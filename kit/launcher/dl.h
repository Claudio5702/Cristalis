// DarkLauncher: the window of a native port and its setup screen ("launcher"). The game gives a
// DlGame (its picture, its sound, one function per frame) and may add its own pages and settings;
// DarkLauncher does the rest: window, fullscreen, settings file, navigation bar and pages (keyboard,
// mouse, controller), languages, picture filters, sound output, save states, screenshots, pause menu,
// --ui-audit.
//
// Every name of the project comes from project.h (found on the include path).
#ifndef DL_H
#define DL_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <wchar.h>

// Languages of the setup screen, in the order of every table of texts: jp, fr, en, es, br, de.
#define DL_LANGS 6
typedef const wchar_t *const DlText[DL_LANGS];

// Buttons of the game's controller (keyboard and XInput pads are read by DarkLauncher).
enum {
  DL_UP = 1 << 0, DL_DOWN = 1 << 1, DL_LEFT = 1 << 2, DL_RIGHT = 1 << 3,
  DL_A = 1 << 4, DL_B = 1 << 5, DL_X = 1 << 6, DL_Y = 1 << 7,
  DL_L = 1 << 8, DL_R = 1 << 9, DL_START = 1 << 10, DL_SELECT = 1 << 11,
};
typedef struct { uint32_t buttons; } DlInput;

// A picture laid over the game's (high-resolution text, a panel...): w x h pixels 0xAARRGGBB, colours
// already multiplied by their opacity, at x,y in the window.
typedef struct { const uint32_t *pixels; int x, y, w, h; } DlOverlay;

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
  // One picture: reads the input, writes width x height pixels 0x00RRGGBB and up to audio_max
  // stereo sample frames; returns how many sample frames it wrote.
  int (*frame)(const DlInput *in, uint32_t *pixels, int16_t *audio, int audio_max);
  void (*reset)(void);             // back to the beginning ("Restart"); may be NULL
  void (*setup)(void);             // registers the game's pages, sections and settings; may be NULL
  // The player starts the game for the first time without a session to take up (the game has been
  // running behind the setup screen until then: an emulator resets the machine here). May be NULL.
  void (*begin)(void);
  void (*quit)(void);              // the program closes (after the session is kept); may be NULL
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
  // Before the game's picture is drawn through the graphics card: fills game (32 floats) and gives its
  // textures with dl_gpu_texture. step of steps: this picture is shown over steps screen refreshes,
  // this is the step-th (1..steps).
  void (*gpu_frame)(float *game, int step, int steps);
  // Screen refreshes for each picture of the game played (smooth motion on a 120 or 240 Hz screen);
  // 1 or NULL: one. With more than one, the screen sets the pace and the sound is stretched by a few
  // samples when it runs short.
  int (*gpu_steps)(void);
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
// A value kept in the settings file; its value now is its default ("Defaults" puts it back unless keep).
void dl_setting(const char *name, int *value, int lo, int hi, bool keep);
// A function of the game (going back to the start of a turn, a panel...): run by a keyboard key and a
// controller button chosen on the Controls pages, and from the pause menu when in_pause. hot: its key's
// name in the settings file (default key: key, 0 none), NULL: no key; pad: the end of its controller
// buttons' names ("_turn": pad_turn and joy_turn), NULL: no button. At most 8.
void dl_function(const wchar_t *const *label, const wchar_t *const *help, const char *hot, int key, const char *pad,
                 void (*run)(void), bool in_pause);

int dl_lang(void);                              // language of the setup screen, 0-5
const wchar_t *dl_tr(const wchar_t *const *t); // a DlText in that language
void dl_redraw(void);                           // the setup screen has to be drawn again
void dl_note(const wchar_t *text);              // a few words over the game (or in the pause menu) for a moment
void dl_restart(void);                          // the game from the beginning ("Restart"; shown only when the game has reset)
// Closes the program as usual (the session and the settings are kept), then starts it again with the
// same command line: for a change the game can only take at start (another language of its data...).
void dl_relaunch(void);
bool dl_in_game(void);                          // the game is being played (not the setup screen, not the preview before it)
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

// Runs the program: window, setup screen, game. Returns the exit code.
int dl_run(const DlGame *game, int argc, char **argv);

#endif
