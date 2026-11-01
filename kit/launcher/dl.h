// DarkLauncher: the window of a native port and its setup screen ("launcher"). The game gives a
// DlGame (its picture, its sound, one function per frame) and may add its own pages and settings;
// DarkLauncher does the rest: window, fullscreen, settings file, navigation bar and pages (keyboard,
// mouse, controller), languages, picture filters, sound output, save states, screenshots, pause menu,
// --ui-audit. A game that has its own window, picture and sound gives play() instead of frame():
// DarkLauncher is then its launcher only (DlGame.play).
//
// Every name of the project comes from project.h (found on the include path).
#ifndef DL_H
#define DL_H

// The version of DarkLauncher (CHANGELOG.md): 1.01, 1.02... for improvements; the first number moves
// (2.0) when a game has to change its code for it.
#define DL_VERSION "2.98"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <wchar.h>
#ifdef __cplusplus
extern "C" { // a game written in C++ includes it as it is
#endif

// Languages of the setup screen, in the order of every table of texts: jp, fr, en, es, br, de, it, zh
// (Simplified Chinese), ko.
enum { DL_LANG_JP, DL_LANG_FR, DL_LANG_EN, DL_LANG_ES, DL_LANG_BR, DL_LANG_DE, DL_LANG_IT, DL_LANG_ZH, DL_LANG_KO };
#define DL_LANGS 9
// DlGame.langs of a game whose texts are in every language of DarkLauncher.
#define DL_LANGS_ALL ((1u << DL_LANGS) - 1)
// A text in each language, in the order above. A missing one (NULL, or left out at the end) is shown
// in English.
typedef const wchar_t *const DlText[DL_LANGS];

// Buttons of the game's controller (keyboard and XInput pads are read by DarkLauncher).
enum {
  DL_UP = 1 << 0, DL_DOWN = 1 << 1, DL_LEFT = 1 << 2, DL_RIGHT = 1 << 3,
  DL_A = 1 << 4, DL_B = 1 << 5, DL_X = 1 << 6, DL_Y = 1 << 7,
  DL_L = 1 << 8, DL_R = 1 << 9, DL_START = 1 << 10, DL_SELECT = 1 << 11,
  // only for a game that asks for them (DlGame.extra_buttons): the second shoulder buttons (a
  // controller's triggers at first) and the sticks' clicks, as on a PlayStation pad
  DL_L2 = 1 << 12, DL_R2 = 1 << 13, DL_L3 = 1 << 14, DL_R3 = 1 << 15,
};
// A player's input for one frame: the DL_* buttons held, and the sticks (only with DlGame.analog,
// otherwise 0) from -32767 to 32767 as XInput gives them: 0 at rest, right and up positive, after a dead
// zone in the middle (a stick at rest never drifts).
typedef struct {
  uint32_t buttons;
  int16_t lx, ly, rx, ry;
} DlInput;
// Where the game's buttons are on a controller at first (DlGame.pad_layout): in the places of a Super
// Nintendo pad's (DL_A the right face button, DL_B the bottom one), or of an Xbox pad's (DL_A the
// bottom one, DL_B the right one, DL_X the left one, DL_Y the top one), PlayStation pads alike.
enum { DL_PAD_SNES, DL_PAD_XBOX };
// What opens the pause menu on a controller at first (DlGame.pad_menu): View, which is also the game's
// Select; View held for a moment (a short press stays the game's Select); no button (Esc on the
// keyboard; the player may still choose one on the Controls page).
enum { DL_MENU_VIEW, DL_MENU_HOLD, DL_MENU_NONE };
// What a game does not offer (DlGame.hide): the CRT filter, the scanlines (an HD game made for flat
// screens...); DarkLauncher's fast forward (a game that has its own, or runs on a thread of its own and
// cannot give several pictures at once): its speed, its key, its controller button.
enum { DL_LOOK_PIXELS, DL_LOOK_RETRO3D, DL_LOOK_MODERN }; // DlGame.look (2.81): 2D pixel art, an old console's 3D, modern 3D
enum { DL_HIDE_CRT = 1 << 0, DL_HIDE_SCANLINES = 1 << 1, DL_HIDE_TURBO = 1 << 2, DL_HIDE_GAMES = 1 << 3 }; // GAMES (2.71): no "Other games"

// A picture laid over the game's (high-resolution text, a panel...): w x h pixels 0xAARRGGBB, colours
// already multiplied by their opacity, at x,y in the window.
typedef struct { const uint32_t *pixels; int x, y, w, h; } DlOverlay;

// Styles of DlTheme: what DarkLauncher draws itself, chosen to match the game's art direction. 0 is
// always DarkLauncher's own.
enum { DL_FRAME_ROUND, DL_FRAME_SQUARE, DL_FRAME_DOUBLE, DL_FRAME_SOFT };  // panels: rounded with a line; square; a double line (an RPG window); very round without a line
enum { DL_CURSOR_BAR, DL_CURSOR_ARROW, DL_CURSOR_OUTLINE };               // selection: a filled bar; a pointer before a faint bar; an outline
enum { DL_SKY_CLOUDS, DL_SKY_GRADIENT, DL_SKY_STARS, DL_SKY_PIXEL };      // behind the menus: sky and sea of clouds; a plain gradient with a glow; a starry night; the clouds in big pixels with few colours
enum { DL_TITLE_METAL, DL_TITLE_FLAT };                                   // the project's name: slanted metal letters; upright letters in the value colour (or title_colour)
enum { DL_BEEP_BELL, DL_BEEP_CHIP, DL_BEEP_SOFT, DL_BEEP_CRYSTAL };       // menu sounds: soft bell; square wave of old consoles; low muffled click; bright chime

// The look of the setup screen fitted to the game's art direction (DlGame.theme; NULL: DarkLauncher's
// own night blue): its colours, fonts and styles, which DarkLauncher draws itself. Every field may be
// 0 or NULL, and a function returning NULL leaves DarkLauncher's own. The pictures and sounds are for
// art made for the port by the project (drawn or computed), not for files taken from the game: w x h
// pixels 0xAARRGGBB, colours already multiplied by their opacity (as overlays), asked for once start()
// has run, valid while the program runs.
typedef struct DlTheme {
  // 0xRRGGBB, from the game's logo or its own menus (0: DarkLauncher's blue): the panels, the selection,
  // the sky and the edges take its hue and strength, each keeping how light DarkLauncher makes it, so
  // the texts stay readable.
  uint32_t colour;
  uint32_t value;                                // 0xRRGGBB of the values and bars; 0: from colour
  const wchar_t *title_font;                     // the project's name and subtitle (an installed font); NULL: Palatino Linotype
  const wchar_t *text_font;                      // the menus, except in Japanese; NULL: Verdana
  // Behind the menus instead of the sky (the player's launcher_bg.* still wins). Since 2.52, where it
  // is not opaque the sky shows through, moving (DlTheme.sky): a landscape under DarkLauncher's clouds.
  const uint32_t *(*backdrop)(int *w, int *h);
  const uint32_t *(*logo)(int *w, int *h);       // at the top left instead of the project's name
  // A picture for an icon of the navigation bar, known by its Segoe MDL2 Assets character (dl_section;
  // DarkLauncher's own: 0xE768 Play, 0xE74E Saves, 0xE722 Screenshots, 0xE774 Languages, 0xE7F4 Picture,
  // 0xE767 Sound, 0xE765 Controls, 0xE8B7 Folder, 0xE7A7 Defaults, 0xE72C Restart, 0xE7E8 Quit), or
  // icon 0: the window's and the taskbar's. Since 1.32 the bar shows 0xE713 Settings, which opens
  // Languages, Picture, Sound, Controls, Folder, Defaults and Restart as rows of a page.
  const uint32_t *(*icon)(unsigned icon, int *w, int *h);
  // The menus' sounds (kind 0 moving, 1 choosing): n mono 16-bit samples, rate per second.
  const int16_t *(*sound)(int kind, int *n, int *rate);
  int frame, cursor, sky, title, beep; // DL_FRAME_*, DL_CURSOR_*, DL_SKY_*, DL_TITLE_*, DL_BEEP_*
  // Fields added later come here, at the end: a game may fill DlTheme by position.
  // With DL_TITLE_FLAT (2.53): 0xRRGGBB of the project's name (0: value), or of its first title_split
  // characters only, counted from its start (PROJECT_LOGO_FIRST, then PROJECT_LOGO_REST), the rest in
  // value. The rule under the name: title_colour for the whole name, value otherwise.
  uint32_t title_colour;
  int title_split;
  // With DL_SKY_PIXEL (2.70): the size of the sky's pixels, in pixels of the 640 x 400 canvas (a backdrop
  // of 1280 x 800 has two of its pixels to one: 1.5 matches a pixel-art backdrop drawn in blocks of three).
  // 0: 400 / 120, as before.
  double sky_pixel;
} DlTheme;

typedef struct DlGame {
  int width, height;               // size of the picture in pixels (the largest one, see dl_picture_size)
  double aspect;                   // width / height of the picture as shown; 0: square pixels
  double fps;                      // pictures per second
  int audio_rate;                  // stereo 16-bit sample frames per second; 0: no sound
  // true: a picture is made whenever the sound waiting to be played runs short, so the sound card's
  // clock sets the pace (emulators: the game's own rate, no crackles over long sessions); fps is then
  // only used for fast forward. false: fps pictures per second by the computer's clock.
  bool pace_by_sound;
  const wchar_t *const *subtitle;  // DlText under the project's name (what the program is), or NULL
  // Once, before the first frame; false: the program cannot run (the game has said why). May be NULL.
  bool (*start)(void);
  // One picture: reads the input (in[0]; in[1] too, the second player's, with players 2), writes width
  // x height pixels 0x00RRGGBB and up to audio_max stereo sample frames; returns how many sample frames
  // it wrote.
  int (*frame)(const DlInput *in, uint32_t *pixels, int16_t *audio, int audio_max);
  void (*reset)(void);             // back to the beginning ("Restart"); may be NULL
  void (*setup)(void);             // registers the game's pages, sections and settings; may be NULL
  // The player starts the game for the first time without a session to take up (the game has been
  // running behind the setup screen until then: an emulator resets the machine here). May be NULL.
  void (*begin)(void);
  void (*quit)(void);              // the program closes (after the session is kept); may be NULL
  // After a crash (the program did not close normally last time), DarkLauncher starts in safe mode for
  // that run: software display, no HDR, no CRT filter, no vertical sync, in a window, and a page that
  // offers to go on, to resume the latest automatic save or to restart normally. safe_mode, if given,
  // is called then (before start()) to turn off the game's own risky options by changing their values;
  // the settings file keeps the player's values unless the player changes them in that run. May be NULL.
  void (*safe_mode)(void);
  // A line for the crash report (the game's version, where the player is: "chapter 3, battle"...), at
  // most n characters: asked every ten seconds while the game runs, kept in case the program does not
  // close normally, and written then in "<settings file>.crash.txt" (the safe mode page copies it).
  // Quick, and in plain ASCII or UTF-8. May be NULL.
  void (*report)(char *out, int n);
  // What the player is doing, for the status Discord shows (the Internet page of the Controls, with
  // DISCORD_APP_ID in project.h): "Chapter 3, a battle"..., at most n characters of UTF-8. Asked every
  // fifteen seconds. NULL: "Playing" or "In the menus".
  void (*status)(char *out, int n);
  // true: fast forward now, without the key (the game knows a dull moment: the enemy's turn...). May be NULL.
  bool (*turbo)(void);

  // ---- save states (all optional)
  // The game writes or reads its whole state in a file; true when it worked. NULL: no save states (no
  // slots, no page, no shortcuts for them).
  bool (*save_state)(const char *path);
  bool (*load_state)(const char *path);
  // The whole state in memory: its size, a copy out, a copy back in. Gives the rewind (a key held goes
  // back up to 20 seconds) and the picture of a save state that was written without one.
  size_t (*state_size)(void);
  void (*state_to_mem)(void *out);
  void (*state_from_mem)(const void *in);
  void (*state_loaded)(void);      // after a state was loaded or the game rewound (may be NULL)
  // true: DarkLauncher may run the game's frames ahead and take them back ("run-ahead": the
  // Responsiveness row of the Controls page shows the picture one or two frames ahead of the input, so
  // that the game answers sooner). Needs state_size, state_to_mem and state_from_mem. The frames ahead
  // are shown but not heard, and taken back with state_from_mem (state_loaded is not called for them).
  // While one runs, dl_running_ahead() is true: the game leaves out there what must happen once only
  // (recording a replay, marking a cartridge save to write...); DarkLauncher leaves out its notes and
  // achievements.
  bool run_ahead;
  // Where the states are (MAX_PATH characters): n 0-9 the player's slots, 10 + k the game's automatic
  // states, -1 the session. NULL: saves\<FILE_STEM>.state<n>, .auto<k>, .session.
  void (*state_path)(int n, char *out);
  int auto_slots;                              // states the game writes itself, listed after the slots
  const wchar_t *const *const *auto_labels;    // their names (a DlText each)
  // true: the game in progress is kept when the program closes, and "Play" becomes "Continue".
  bool resume;
  int (*play_seconds)(void);       // time played, told beside the session (may be NULL)

  // Pictures over the game's (may be NULL): given where the game's picture is in the window (x, y, w,
  // h in pixels), fills up to max overlays and returns how many. Not in screenshots.
  int (*overlays)(int x, int y, int w, int h, DlOverlay *out, int max);

  // ---- the graphics card (all optional): the game's own part of the picture's shader, for what it
  // draws at the screen's resolution (Kahna: mode 7 in high resolution, smooth motion)
  // HLSL defining float3 scene(float2 q): the colour at q (0-1 across the game's picture), which it may
  // build from fetch(q) (the picture with the chosen filter; texture tex, texSize its size, srcSize the
  // console's resolution, outSize the size on screen). It may declare Texture2D t1-t7 and read the constants game[0..7] (float4). The
  // CRT filter, the scanlines and HDR are applied to what it returns.
  const char *shader;
  // The same scene(q) in GLSL 3.30 for Linux (OpenGL): vec2, vec3, fetch(q), the constants game[0..7] (vec4)
  // and the picture's sizes as above; the textures tex and t1-t7 (sampler2D) are declared already.
  // NULL: on Linux the picture is drawn as it is.
  const char *shader_glsl;
  // Before the game's picture is drawn through the graphics card: fills game (32 floats) and gives its
  // textures with dl_gpu_texture. step of steps: this picture is shown over steps screen refreshes,
  // this is the step-th (1..steps). Under the pause menu (2.98) the picture is drawn again only when
  // something changes, not 60 times a second: the game is paused, what it gives here stays the same.
  void (*gpu_frame)(float *game, int step, int steps);
  // Screen refreshes for each picture of the game played (smooth motion on a 120 or 240 Hz screen);
  // 1 or NULL: one. With more than one, the screen sets the pace and the sound is stretched by a few
  // samples when it runs short.
  int (*gpu_steps)(void);
  // The game draws its picture itself with the graphics card (a 3D scene...) instead of the picture
  // with the filters, while it is played and under the pause menu (not in the setup screen's preview,
  // not in "Try it full size"). dev, ctx, rtv: the ID3D11Device*, ID3D11DeviceContext* and
  // ID3D11RenderTargetView* of the window; x, y, w, h: where the picture goes, in the window's pixels.
  // The game sets its own viewport and states; DarkLauncher sets its own again afterwards. hdr: 0,
  // colours as usual (8 bits, gamma); otherwise the output is HDR (16-bit floating point, linear light,
  // 1.0 = 80 cd/m2) and hdr is the brightness of white in those units. step of steps as for gpu_frame.
  // False: the picture is drawn as usual this time. The overlays, the frame rate, the notes and the
  // pause menu go over it, and screenshots take it. Without the graphics card, the picture as usual.
  bool (*gpu_draw)(void *dev, void *ctx, void *rtv, int x, int y, int w, int h, float hdr, int step, int steps);

  const DlTheme *theme; // the setup screen's look from the game (may be NULL)
  int pad_layout;       // DL_PAD_*: the controller's first assignment (0: Super Nintendo places)
  int pad_menu;         // DL_MENU_*: what opens the pause menu on a controller (0: View)
  bool rumble;          // the game makes the controller vibrate (dl_rumble): a Vibration switch on the Controls page

  // The game's own version ("1.2"), and a DlText of what is new in it: shown on a page the first time
  // this version starts (the settings file keeps which one was seen). One line (\n between them) for
  // each news: a short title (a row of the page), then | and what it is (its help). May be NULL.
  const char *version;
  const wchar_t *const *news;

  // The languages of the game's own texts (its DlTexts, and the tables it reads with dl_lang()): bits
  // 1 << DL_LANG_*, or DL_LANGS_ALL (Chinese and Korean too since 2.90: a game that gives it must have
  // them). 0: the first six (jp to de), the languages before Italian. The Languages page offers only
  // these, and dl_lang() never returns another.
  unsigned langs;

  // DL_HIDE_*: rows left out (0: all shown). What they set is not applied then (the picture is drawn
  // without it, no fast forward, not even from turbo()), and the presets leave it aside ("CRT" goes with
  // the CRT filter); the settings file keeps the player's values.
  unsigned hide;

  // The game runs outside DarkLauncher (2.20), which is then its launcher only: the setup screen without
  // the game inside (another program plays it, or this one started again without its launcher). "Play"
  // writes the settings file, hides DarkLauncher's window and calls play(), which returns once the game
  // is over; the setup screen comes back, with the settings file read again (the game may have changed
  // it). false: the game could not start, and a note says so. frame, width, height, fps and audio_rate
  // may then be 0, and DarkLauncher offers only what concerns its own menus: no preview, no save states,
  // screenshots, pause menu, Picture, Sound or Controls pages (the game's own pages say how it plays).
  // NULL: the game runs inside DarkLauncher, as before.
  bool (*play)(void);

  // ---- a controller with more (2.30, all optional; a PlayStation game's)
  // DL_L2, DL_R2, DL_L3, DL_R3 that the game reads (0: the twelve buttons only): their rows join the
  // Controls pages (keyboard and controller), at first on the triggers and the sticks' clicks.
  unsigned extra_buttons;
  // true: DlInput's sticks are filled, and the left stick no longer doubles as the directional pad.
  bool analog;
  // 2: two players, frame() gets in[0] and in[1]. Player 1 has the keyboard and the first controller,
  // player 2 the second one; a row of the Controls page gives player 2 the first controller instead,
  // player 1 keeping the keyboard. Both controllers share the controller assignments. 0 or 1: one
  // player, with the keyboard and every controller.
  int players;

  // Fields added later come here, at the end: a game may fill DlGame by position.
  // ---- the first start (2.62, optional): a page of the game's own, the last of the first start's
  // pages (the language, the controls, the screen, then this one; with play, the language then this
  // one), which DarkLauncher ends with "Start playing". Its rows as a page's (DL_CHOICE with show and run,
  // a help each...). NULL tab or no rows: no such page.
  const wchar_t *const *welcome_tab; // its tab (a DlText)
  const struct DlRow *welcome_rows; // (DlRow, declared below)
  int welcome_count;
  // Draws in the place of the preview for row k of that page and returns true (as DlList.preview: x0,
  // y0, x1, y1 on the 640 x 400 canvas, dc an HDC, dl_ui_text and dl_ui_box); false, or NULL: the live
  // picture.
  bool (*welcome_preview)(void *dc, int k, double x0, double y0, double x1, double y1);
  // ---- achievements' pictures (2.63, optional): the picture of the achievement id, w x h pixels
  // 0xAARRGGBB with colours multiplied by their opacity (as the theme's), valid while the program runs;
  // asked once start() has run, once each. Shown on the Achievements page (in grey until unlocked; a
  // secret one keeps the locked star) and in "Achievement unlocked". NULL, or NULL returned: the star.
  const uint32_t *(*achievement_picture)(const char *id, int *w, int *h);
  // ---- the start (2.64, optional): a picture shown in the preview, "Starting the game…" over it, while
  // the game has given no real picture yet (dl_picture_same; 0xAARRGGBB premultiplied, valid while the
  // program runs; asked once). NULL, or NULL returned: the text alone.
  const uint32_t *(*starting_picture)(int *w, int *h);
  // ---- xBR (2.68, optional): true when bit 0x80 of the top byte of each pixel says what the xBR
  // filter may round (2.69; the other seven bits stay the game's own): set, a pixel of a character or an
  // object (sprites); clear, the painted scenery, kept exactly as drawn (its noise, its dithers and a
  // veil moving over it would turn into squiggles). A shared picture (dl_picture_shared,
  // dl_picture_target) says it in its texture's alpha (0.5 or more: set).
  bool xbr_mask;
  // ---- the mouse in play (2.74, optional): true for a game played with the mouse too (dl_mouse). The
  // pointer is then shown over the picture while it moves, even in console mode, and hidden again after
  // three seconds without moving. false: as before (shown, except in console mode).
  bool mouse_in_game;
  // ---- the kind of picture (2.81, DL_LOOK_*): what the Picture page offers. DL_LOOK_PIXELS (0: as
  // before): pixel art, every row. DL_LOOK_RETRO3D: an old console's polygons at a low resolution: Sharp
  // or Smooth only (Smooth edges and xBR are made for drawn pixels), integer scaling off at first,
  // scanlines and CRT kept. DL_LOOK_MODERN: a high-definition 3D picture: smoothed at any size, without
  // integer scaling, scanlines, CRT nor the presets (nor their rows). The game's own options (render
  // resolution, anti-aliasing...) stay on its own pages.
  int look;
  // ---- the pace set by the game (2.82, optional): true for a game whose frame() takes the time of
  // its own picture (a recompilation at 30, 120, 240 pictures a second, or a varying rate): frame() is
  // called again at once, fps is then the most pictures a second (0: no limit), and the screen shows the
  // newest picture at each of its refreshes (a picture that comes sooner waits, the one before it is
  // left out; on Windows the swap chain never waits for the screen). frame() gives the sound it made in
  // its own time. No smooth motion over several refreshes, no fast forward, no run-ahead then;
  // pace_by_sound is not used.
  bool pace_by_game;
  // ---- fast forward's speed at first (2.96, optional): 2 or 4 (times), what the Controls page shows
  // before the player chooses and after "Defaults"; 0: Max, as before (as fast as the computer goes,
  // which differs from one to the next). The player's choice is kept in the settings file.
  int turbo_default;
} DlGame;

// ---- pages: lists of rows, drawn and handled by DarkLauncher
typedef enum {
  DL_ACTION, // does something when chosen (run); show(0), if any, gives a text beside it
  DL_SWITCH, // *value 0 or 1, drawn as a switch
  DL_CHOICE, // *value 0..max, its text from names[value] or show(value), changed with < >
  DL_BAR,    // *value 0..max, drawn as a bar
  DL_PAGE,   // opens another page
  DL_KEY,    // *value: a keyboard key (virtual-key code); Enter waits for the new key
  DL_PADKEY, // *value: a controller button (XInput: 0-15 bits of the button word, 16 LT, 17 RT; -1 none);
             // Enter waits for the new button; max 1: Delete leaves it unassigned
} DlKind;

typedef struct DlRow {
  DlKind kind;
  const wchar_t *const *label, *const *help; // DlText
  int *value;
  int max;
  const wchar_t *const *const *names;        // DL_CHOICE: max + 1 DlTexts, or NULL
  const wchar_t *(*show)(int value);         // DL_CHOICE without names: the text of a value
  void (*run)(void);                         // DL_ACTION: the action; others: after a change (may be NULL)
  int page;                                  // DL_PAGE: the page to open
  const wchar_t *(*more)(void);              // text added under the help (a state of the moment), or NULL
  // The game takes this value when the program starts only (its resolution, a mod...). Once such a row
  // differs from its value at start, the pages that have one show "Restart to apply" (dl_relaunch, back on
  // the same page), and "Play" starts the program again before playing (2.67). Its help may say so.
  bool at_start;
} DlRow;

// A page whose rows the game makes each time it is shown (saves of its own, live values...). The rows
// scroll when there are more than fit.
typedef struct DlList {
  void (*open)(void);                                 // reads what the list shows, when its page is shown
  int (*count)(void);                                 // rows, before "Back"
  void (*row)(int k, wchar_t *label, wchar_t *value); // texts of row k (64 characters each; value "": none)
  const wchar_t *(*help)(int k);
  void (*enter)(int k);                               // Enter or a click (may be NULL)
  void (*erase)(int k);                               // Delete, or Y on the controller (may be NULL)
  void (*moved)(void);                                // the selection moved (may be NULL)
  const wchar_t *const *empty;                        // DlText: the help when the list is empty (may be NULL)
  // Draws in the place of the live picture for row k (x0, y0, x1, y1 on the 640 x 400 canvas, with
  // dl_ui_text and dl_ui_box) and returns true; false, or NULL: the live picture. dc is an HDC.
  bool (*preview)(void *dc, int k, double x0, double y0, double x1, double y1);
} DlList;

// Pages DarkLauncher makes before calling setup(): the game may add rows to them (they come first).
enum { DL_PAGE_LANGS, DL_PAGE_IMAGE, DL_PAGE_SOUND };

// A new page (a "Back" row is added after its rows); returns its id.
int dl_page(const wchar_t *const *tab, const DlRow *rows, int count);
int dl_list_page(const wchar_t *const *tab, const DlList *list);
// Rows put at the top of an existing page.
void dl_add_rows(int page, const DlRow *rows, int count);
// An entry of the navigation bar opening a page (icon: a character of the Segoe MDL2 Assets font).
void dl_section(int page, unsigned icon, const wchar_t *const *label, const wchar_t *const *help);
// A row at the end of the Saves page (before "Back") opening one of the game's pages: its own saves, for
// instance (a cartridge's), next to the save states rather than in an entry of the bar of their own.
// Esc on that page comes back to this row. One such row; a second call replaces it.
void dl_saves_more(int page, const wchar_t *const *label, const wchar_t *const *help);
// Before the game writes over one of its own saves (a cartridge's...): DarkLauncher keeps the version
// there now in saves\backup\ (3 versions of each file at most, a new one when the last is 10 minutes
// old), and the "Backups" row of the Saves page puts one back (the program then starts again, so that
// the game reads it). The save states and the session are kept that way by DarkLauncher itself.
void dl_backup(const char *path);
// A value kept in the settings file; its value now is its default ("Defaults" puts it back unless keep).
void dl_setting(const char *name, int *value, int lo, int hi, bool keep);
// A function of the game (going back to the start of a turn, a panel...): run by a keyboard key and a
// controller button chosen on the Controls pages, and from the pause menu when in_pause. hot: its key's
// name in the settings file (default key: key, 0 none), NULL: no key; pad: the end of its controller
// buttons' names ("_turn": pad_turn and joy_turn), NULL: no button. At most 8.
void dl_function(const wchar_t *const *label, const wchar_t *const *help, const char *hot, int key, const char *pad,
                 void (*run)(void), bool in_pause);

// ---- achievements (optional)
typedef struct DlAchievement {
  const char *id;                            // its name in the file that keeps them (letters, digits, _)
  const wchar_t *const *name, *const *desc;  // DlTexts
  bool hidden;                               // name and description secret until it is unlocked
} DlAchievement;
// In setup(): the game's achievements (at most 128). DarkLauncher adds the Achievements page and its
// entry in the bar, and keeps which are unlocked and when in saves\<FILE_STEM>.achievements next to the
// program. The list must stay valid while the program runs.
void dl_achievements(const DlAchievement *list, int count);
// Unlocks one, once: "Achievement unlocked" and its name over the game for a few seconds, with a sound.
void dl_unlock(const char *id);
bool dl_unlocked(const char *id);

// ---- packs and mods (optional): folders, or files of the game's kinds, that the player drops into a
// folder; a page turns each on or off and orders them (the ones above come before the ones below)
typedef struct DlPacks {
  const char *folder;           // where the player puts them: a folder next to the program ("packs"), or a full path
  const char *const *exts;      // files that are packs, by extension ({".zip", NULL}); folders always are; NULL: folders only
  const wchar_t *const *tab, *const *label, *const *help; // DlTexts of the page and of its entry in the bar
  unsigned icon;                // its entry in the bar (a Segoe MDL2 Assets character); 0: none (a DL_PAGE row of the game opens the page)
  // The packs turned on or their order changed: the game reads them again now. NULL: the game reads
  // them at start only, and the page offers to restart the program.
  void (*changed)(void);
} DlPacks;
// In setup(): the Packs page (its id is returned) and its entry in the bar; the packs are read at once,
// and their choice is kept in "<settings file>.packs".
int dl_packs(const DlPacks *p);
int dl_pack_count(void);         // the packs turned on, the one that comes first first
const char *dl_pack_path(int k); // full path of pack k (a folder or a file), in the Windows code page

// The controller's vibration the game wants now: left the heavy motor, right the light one, 0-65535 (as
// XInput's). Sent to the Xbox pads and the PlayStation ones; nothing while the menus are shown, the window
// is behind another or the player has turned Vibration off. Call it again with 0, 0 to stop.
void dl_rumble(int left, int right);

int dl_lang(void);                              // language of the setup screen (DL_LANG_*), one of DlGame.langs
const wchar_t *dl_tr(const wchar_t *const *t); // a DlText in that language
void dl_redraw(void);                           // the setup screen has to be drawn again
void dl_note(const wchar_t *text);              // a few words over the game (or in the pause menu) for a moment
void dl_restart(void);                          // the game from the beginning ("Restart"; shown only when the game has reset)
// Closes the program as usual (the session and the settings are kept), then starts it again with the
// same command line: for a change the game can only take at start (another language of its data...).
// It comes back to the page of the setup screen it was called from (2.67), or into the game when it
// was "Play" (rows marked at_start), without the one-time options (--play...).
void dl_relaunch(void);
bool dl_in_game(void);                          // the game is being played (not the setup screen, not the preview before it)
// The mouse over the game's picture (2.74), from frame(): *x, *y in the picture's pixels (as frame()
// writes them, 0,0 its top left corner; the window's size, the black bands, the integer scale and the
// filters taken away, not a curved CRT's curve), *buttons held now (bit 0 left, 1 right, 2 middle) or
// pressed since the last call too, so that a tap let go before this picture is seen once; a touch screen
// is the mouse, a finger its left button, held as long as it stays (2.96, Windows and Linux), *wheel the
// notches turned since the last call (up: positive; given to a frame run ahead as 0). false,
// *buttons and *wheel 0: not in play, the window not in front, or the pointer not over the picture.
// DarkLauncher takes no click and no notch in play (the pause menu stays on its key). Any may be NULL.
bool dl_mouse(double *x, double *y, unsigned *buttons, int *wheel);
bool dl_running_ahead(void);                    // from frame(): this frame is one run ahead, to be taken back (DlGame.run_ahead)
// From frame() (2.97): the picture of this frame will not be shown, so the game may leave out its drawing
// (run-ahead: the frame taken for good and the middle frames ahead; only the last one ahead gives the
// picture). Its sound and everything else are needed as usual. DarkLauncher shows, saves and compares
// only pictures of frames where it is false.
bool dl_picture_unused(void);
// The game's own button prompts (2.94): what the player uses now, in play too (the device of the last key,
// controller button or, for a game with mouse_in_game, mouse move or click in play), and a picture of one
// of the game's buttons (a DL_* bit, as DlInput.buttons) as that device shows it: the Xbox letter, the
// PlayStation symbol, another controller's button number, a key cap with the assigned key's name (the
// player's own assignments: a change shows), the directional pad with that direction lit; with the
// mouse, DL_A and DL_B as its left and right buttons (the others as keys). size is its height in pixels
// (8 to 256); *w (the width, wider for a long name) and *h are set. 0xAARRGGBB, colours multiplied by
// their opacity (as DlOverlay). The picture stays until dl_input_serial() changes (the device or an
// assignment changed: ask again), NULL for a button with nothing assigned. From frame() or the game's
// loop (DarkLauncher's thread).
enum { DL_DEVICE_KEYBOARD, DL_DEVICE_MOUSE, DL_DEVICE_XBOX, DL_DEVICE_PLAYSTATION, DL_DEVICE_OTHER };
int dl_input_device(void);
unsigned dl_input_serial(void);
const uint32_t *dl_button_picture(unsigned button, int size, int *w, int *h);
// The proportions (width / height) of the area the game's picture is shown in now (2.95): the window's
// inside, the whole screen in full screen (console mode too); it changes when the window is resized or
// goes full screen. A game that can draw at any proportions (its backgrounds widened) gives a picture of
// these with dl_picture_size and fills the screen without black bands. With the integer scale on, bands
// may stay (the picture's size is a whole multiple). From frame() or the game's thread.
double dl_view_aspect(void);
bool dl_gpu_on(void);                           // the picture goes through the graphics card now
void dl_open_page(int page);                    // shows a page of the setup screen (from a list, the pause menu...)
// The game is not started any more (another game was loaded: another language...): it runs behind the
// setup screen again, and "Play" begins it (begin, or its session) as at the first start.
void dl_stop(void);
void dl_resume(void); // back to the game (from the pause menu or the setup screen), once it is started
// Drawing in a list's preview: logical 640 x 400 coordinates, colours 0xRRGGBB; align 0 left, 1
// centre, 2 right; small: the smaller font.
void dl_ui_text(void *dc, double x, double y, const wchar_t *s, uint32_t rgb, int align, bool small);
void dl_ui_box(void *dc, double x0, double y0, double x1, double y1, uint32_t rgb, double radius);

// From DlGame.gpu_frame: texture t<slot> (1-7) of the game's shader, w x h texels.
enum { DL_TEX_BGRA8, DL_TEX_R8, DL_TEX_RGBA32F };
bool dl_gpu_texture(int slot, int format, const void *data, int w, int h);
// Refresh rate of the screen that shows the window (read again every two seconds), for gpu_steps.
int dl_screen_hz(void);

// From frame(): the picture just written is w x h pixels (at most width x height, w pixels per row),
// shown with the proportions aspect (width / height on screen; 0: square pixels). Until it is called,
// the picture is width x height with the game's aspect. For a picture that widens (widescreen) or
// grows (high-resolution graphics) while the game runs.
void dl_picture_size(int w, int h, double aspect);
// From frame(): the picture is s times the console's own resolution (enlarged graphics); the
// scanlines and the CRT filter follow the console's lines. 1 until it is called.
void dl_picture_scale(int s);
// From frame(): the picture is the one given last time, unchanged (the game had no new one in time: it
// waits for another thread, it is paused...). Optional: --bench and --gpu-check then count the new
// pictures apart, so that a game that stops making them shows at once. Until a game's first picture
// with something in it (not black, or in a shared texture), the preview says "Starting the game…"
// instead of staying black (2.55; a minute at most): a console that takes seconds to start.
void dl_picture_same(void);
// From frame(), instead of writing the pixels (2.40): the picture is in a Direct3D 11 texture the game
// shares with DarkLauncher, which shows it without a copy through the processor (a game that draws with
// a renderer of its own, on a device of its own: a recompilation's). The texture, made by the game on the
// default graphics card: DXGI_FORMAT_B8G8R8A8_UNORM or R8G8B8A8_UNORM (or their TYPELESS kinds), with
// D3D11_BIND_SHADER_RESOURCE and D3D11_RESOURCE_MISC_SHARED_NTHANDLE | D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX;
// handle: from its IDXGIResource1::CreateSharedHandle, kept open while the game gives it (DarkLauncher
// opens it again when its own graphics card path starts again; a new texture, a new handle). The game
// writes the texture between AcquireSync(0) and ReleaseSync(0) of its IDXGIKeyedMutex, and DarkLauncher
// reads it the same way. The picture is the whole texture (at most width x height), aspect as for
// dl_picture_size. Called for every picture in the texture, the same one again too (dl_picture_same).
// False: DarkLauncher cannot show it now (no graphics card path, another graphics card, Linux...): the
// game writes the pixels this time, as usual.
bool dl_picture_shared(void *handle, double aspect);
// From frame(), for a game that draws with Direct3D 12 or Vulkan (or Direct3D 11) on a device of its own
// (2.50): DarkLauncher makes the texture the picture goes in and a fence, both shared, and shows the
// picture without a copy through the processor. Asked for each picture, before the game draws it: w x h
// (at most width x height; another size, another texture), aspect as for dl_picture_size. True: on its
// graphics card queue the game waits for the fence to reach t->wait, writes the whole picture into the
// texture, then sets the fence to t->signal; always, the same picture again too, since DarkLauncher waits
// for that value (a game that has not set it half a second later is given false from then on).
// DarkLauncher reads the texture only between two calls of frame(). False: not possible now (no graphics
// card path, Linux...): the game writes the pixels this time, as usual.
typedef struct DlTarget {
  // NT handle of the texture: DXGI_FORMAT_R8G8B8A8_UNORM, one level, usable as a render target, a copy's
  // destination or an unordered access view. Direct3D 12: ID3D12Device::OpenSharedHandle (an
  // ID3D12Resource, in the state COMMON before and after); Vulkan: an image's dedicated memory imported as
  // VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D11_TEXTURE_BIT, its queue family taken from and given back to
  // VK_QUEUE_FAMILY_EXTERNAL; Direct3D 11: ID3D11Device1::OpenSharedResource1.
  void *texture;
  // NT handle of the fence. Direct3D 12: OpenSharedHandle (an ID3D12Fence); Vulkan: a timeline semaphore
  // imported as VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_D3D12_FENCE_BIT; Direct3D 11: ID3D11Device5::OpenSharedFence.
  void *fence;
  uint64_t wait, signal;
  // The graphics card DarkLauncher draws with, where the game makes its device: DXGI's AdapterLuid,
  // Vulkan's VkPhysicalDeviceIDProperties::deviceLUID.
  uint8_t luid[8];
  // Changes when the texture or the fence is a new one: the game opens them again. The handles stay
  // DarkLauncher's, open until then; the game closes neither.
  int serial;
} DlTarget;
bool dl_picture_target(int w, int h, double aspect, DlTarget *t);

// Runs the program: window, setup screen, game. Returns the exit code.
int dl_run(const DlGame *game, int argc, char **argv);

// ---- the menus over the game (2.80): for a game that keeps its own window, renderer, pace and HDR (a
// modern recompilation). DarkLauncher has no window nor loop then: the game's loop calls it, and lays
// its menus, a picture with opacity, over its own picture. What DlGame gives is used as with dl_run
// (setup and its pages, settings, theme, achievements, functions, begin, reset, save states...), but no
// frame(): the picture, the sound and the pace are the game's. No Picture or Sound pages, no preview,
// no screenshots by DarkLauncher (as with DlGame.play).
//   The game's loop: at start dl_over_start(); each picture dl_over_turn(): while it says DL_OVER_MENU
//   the player's input is DarkLauncher's, while it says DL_OVER_PAUSE the game waits (before the first
//   "Play" it may run behind the menus, its title as inside DarkLauncher); dl_over_layer() when it says
//   DL_OVER_LAYER, laid over the game's picture (one quad, premultiplied alpha); every message of its
//   window to dl_over_message() first; at the end (DL_OVER_QUIT, or the game's own) dl_over_end().
// window: the game's HWND on Windows. On Linux (2.92) its SDL_Window (SDL3), DarkLauncher never drawing
// into it nor reading SDL's events: the game gives each event it polls to dl_over_sdl_event() first
// (or a window of DarkLauncher's stand-ins of Windows' API, given messages by dl_over_message, as the
// demo's --over). Returns -1 to go on, otherwise the exit code to end the program with at once
// (--version, --ui-audit, the game's start() said no...).
int dl_over_start(const DlGame *game, int argc, char **argv, void *window);
enum {
  DL_OVER_MENU = 1,  // DarkLauncher's menus are shown (the setup screen, the pause menu): the player's input is theirs
  DL_OVER_PAUSE = 2, // the game waits: menus over the game once started, or the window behind another one with "Pause in background"
  DL_OVER_LAYER = 4, // the layer changed: take it again (dl_over_layer)
  DL_OVER_QUIT = 8,  // the player chose Quit (or the program must start again): end the game, then call dl_over_end
};
unsigned dl_over_turn(void);
// The layer: w x h (the window's client area) pixels 0xAARRGGBB, colours multiplied by their opacity,
// valid until the next call. Transparent where nothing is shown (in play: only a note or "Achievement
// unlocked" there).
const uint32_t *dl_over_layer(int *w, int *h);
// A message of the game's window (its window procedure's four values), given first: true when
// DarkLauncher took it (a key or a click for its menus, its pause key in play; the pointer hidden in
// console mode), and the window procedure then returns TRUE without doing anything else with it.
bool dl_over_message(void *window, unsigned msg, uintptr_t wparam, intptr_t lparam);
// Linux, a game with its own SDL window (2.92): each SDL_Event it polls (its address), given first; true
// when DarkLauncher took it (as dl_over_message: a key or a click for its menus, its pause key in play),
// and the game then does nothing else with it. Its quitting and its window's closing stay the game's;
// while DL_OVER_MENU, the input that is not taken (key releases, pads' buttons) is the game's to leave.
// On Windows it takes nothing.
bool dl_over_sdl_event(const void *event);
// The players' buttons as the Controls give them (keyboard and controllers), for a game that reads its
// input through DarkLauncher; zero while a menu shows.
void dl_over_input(DlInput in[2]);
// The end: the settings and the session kept, the running mark removed; the program started again
// if that was asked for (dl_relaunch, an update). Returns the exit code.
int dl_over_end(void);

#ifdef __cplusplus
}
#endif
#endif
