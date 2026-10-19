// Inside the kit: what its parts share. Not for the game (it uses dl.h).
#ifndef DL_INT_H
#define DL_INT_H
#ifdef _WIN32
#include <windows.h>
#else
#include "linux/win32.h" // GDI and the rest of what the shared code takes from Windows, made for Linux
#endif
#include <stdio.h>
#include "dl.h"
#include "project.h"
// What the project gives for the Internet page (project.h); "" leaves the row out.
#ifndef DISCORD_APP_ID
#define DISCORD_APP_ID "" // its Discord application (discord.com/developers), for the player's status
#endif
#ifndef UPDATE_URL
#define UPDATE_URL "" // a text file (http or https): the newest version on its first line, its page on the second
#endif

// The separator of a path's parts: Windows' own, or "/" elsewhere (the paths given to the game are the
// system's).
#ifdef _WIN32
#define DL_SEP "\\"
#define DL_SEPC '\\'
#else
#define DL_SEP "/"
#define DL_SEPC '/'
#endif

typedef uint8_t u8;
typedef uint32_t u32;
typedef int16_t s16;
typedef int64_t s64;

// ---- settings of the kit (dl_app.c)
typedef struct {
  int ui_lang, fullscreen, integer_scale, smooth, scanlines, show_fps, volume, menu_sound;
  int gpu, crt, hdr, vsync; // graphics card (0: GDI), CRT filter (0 off, 1 flat, 2 curved), HDR white (0 off, 1-4), vertical sync
  int turbo, pause_menu, bg_pause; // fast forward (0 max, 2 2x, 4 4x), Esc opens the pause menu, pause behind another window
  int win_x, win_y, win_w, win_h;
  int rumble;                         // the controller vibrates when the game asks (dl_rumble)
  int ahead;                          // run-ahead: frames the picture is ahead of the input (0-2; DlGame.run_ahead)
  int discord, updates;               // Internet (both off at first): the status on Discord, a look for a newer version at start
  int contrast, cvd, still, big_help; // accessibility: high contrast, colour-blind filter (0 off, 1 protan, 2 deutan, 3 tritan), still sky, large help text
} DlCfg;
extern DlCfg dl_cfg;
void dl_defaults(bool picture_only);

// ---- the program (dl_app.c)
extern HWND dl_hwnd;
extern char dl_base_dir[MAX_PATH];
extern const DlGame *dl_game;
extern u32 *dl_pic;          // the last picture of the game
extern int dl_pic_w, dl_pic_h; // its size
extern double dl_pic_aspect; // its proportions on screen (width / height)
extern int dl_pic_scale;     // its pixels per pixel of the console (dl_picture_scale)
extern bool dl_started;      // the player has started the game (it is paused while the menu is shown)
extern bool dl_running;
extern bool dl_start_done;   // the game's start() has run (the theme's pictures may be asked for)
extern bool dl_safe;         // this run is in safe mode (the last one did not close normally)
void dl_safe_restart(void);  // the player's settings back, then the program started again
void dl_settings_sibling(char *out, const char *ext); // the settings file's path with another extension (MAX_PATH)
bool dl_packs_key(int vk);   // Page Up / Page Down on the Packs page (dl_packs.c); true: used
extern bool dl_no_mouse;
extern const char *dl_audit_dir;
extern int dl_fps_shown;
void dl_fit(int cw, int ch, bool integer, int *w, int *h);
void dl_blit(HDC dc, int x, int y, int w, int h);
void dl_draw_fps(HDC dc, int x, int y, int h); // the frame rate over a picture at x,y, h high
#define DL_MAX_OVERLAYS 4
void dl_draw_overlays(HDC dc, int x, int y, int w, int h); // the game's, over its picture at x,y w x h
void dl_beep(int kind); // 0 move, 1 choose
void dl_toggle_fullscreen(void);
void dl_play(void);      // start or resume the game
void dl_menu_open(void); // from the game to the setup screen
void dl_pause_open(void); // from the game to the pause menu (or the setup screen, as chosen)
bool dl_save_png(const char *path, const u32 *px, int w, int h);

// ---- controls (dl_input.c)
#define DL_BTNS 12 // the game's buttons, in the order of the DL_* bits
#define DL_MAX_FUNCS 8 // functions of the game (dl_function)
enum { FN_TURBO = DL_BTNS, FN_SAVE, FN_LOAD, FN_SHOT, FN_MENU, FN_COUNT, FN_ALL = FN_COUNT + DL_MAX_FUNCS }; // the program's functions after them, then the game's
enum { HK_SAVE, HK_LOAD, HK_PREV, HK_NEXT, HK_SHOT, HK_FULL, HK_REWIND, HK_COUNT };                         // keyboard shortcuts
extern int dl_key_vk[DL_BTNS + 1]; // keyboard: the game's buttons, then fast forward
extern int dl_pad_code[FN_ALL];    // XInput controllers (codes of DL_PADKEY)
extern int dl_joy_code[FN_ALL];    // other controllers (WinMM): button 0-31, -1 none
extern int dl_hot_vk[HK_COUNT];
typedef struct { const wchar_t *const *label, *const *help; void (*run)(void); bool in_pause; int key, def_key; } DlFunc;
extern DlFunc dl_funcs[DL_MAX_FUNCS];
extern int dl_func_n;
extern int *dl_wait;               // the value of a row waiting for a key or a button (NULL: none)
void dl_input_init(void);          // defaults and settings (before the settings file is read)
void dl_keys_default(void);
void dl_pads_default(void);
void dl_hot_default(void);
bool dl_joy_mode(void);
// What the player used last in the menus: the footer shows its buttons.
enum { DL_IN_KEYS, DL_IN_XBOX, DL_IN_PS, DL_IN_PAD, DL_IN_PS_HID }; // DL_IN_PS: by WinMM, DL_IN_PS_HID: read directly (dl_sony.c)
extern int dl_input_kind;
void dl_input_used(int kind);
// Pictures of controller buttons (ui_pad_glyph): GL_NUM + n is button n + 1 of another controller.
enum { GL_DPAD, GL_XA, GL_XB, GL_XX, GL_XY, GL_PS_SQUARE, GL_PS_CROSS, GL_PS_CIRCLE, GL_PS_TRIANGLE, GL_NUM };
int dl_pad_glyph(bool confirm); // the button that chooses (true) or goes back in the menus, for dl_input_kind
void dl_rumble_stop(void);      // at closing time
// PlayStation pads read directly (dl_sony.c): their buttons as XInput codes, their rumble (0-255), and
// looking for them again (Windows says devices came or went).
bool dl_sony_poll(u32 *raw);
void dl_sony_rumble(int left, int right);
void dl_sony_rescan(void);
u32 dl_game_buttons(void);         // DL_* bits held now
void dl_input_release(void);       // the game gets no button until every one has been released
bool dl_turbo_held(void);
void dl_pad_poll(void);            // once per turn of the main loop
bool dl_game_key(int vk);          // a key pressed while the game is played; true: one of the program's
void dl_wait_for(int *value, bool pad);
bool dl_wait_pad(void);
void dl_wait_key(int vk);
int *dl_pad_value(int *value);
const wchar_t *dl_key_name(int vk);
const wchar_t *dl_pad_name(int *value);

// ---- save states, screenshots, notes over the game (dl_saves.c)
#define DL_SLOTS 10
extern int dl_slot;                // slot of the quick save and load
void dl_slot_path(char *out, int n, const char *ext);
bool dl_slot_used(int n);
bool dl_state_save(int n);
bool dl_state_load(int n);
void dl_quick_save(void);
void dl_quick_load(void);
void dl_slot_step(int dir);
void dl_screenshot(void);
void dl_notef(const wchar_t *fmt, int n);
bool dl_rewind_ok(void);           // the game can be rewound
void dl_rewind_snap(void);         // after each picture of the game played
bool dl_rewind_step(void);         // one step back (the rewind key held); false: nothing more
void dl_rewind_end(void);          // the key released
bool dl_session_exists(void);
bool dl_session_resume(void);      // "Continue": the session kept at the last close
void dl_session_keep(void);        // at closing time
int dl_latest_auto(void);          // the newest of the session and the game's automatic states (-1, DL_SLOTS + k), -2: none
const wchar_t *dl_session_help(void);
void dl_saves_osk_sample(bool on); // --ui-audit: the keyboard on screen, as when a slot is named with a controller
bool dl_saves_key(int vk);         // a key on the Saves page while a slot's name is typed; true: used
void dl_saves_char(wchar_t c);
void dl_state_loaded(void);
extern bool dl_state_taken;        // a state was loaded before the game was started: no begin()
const wchar_t *dl_note_now(void);  // the note being shown, or NULL
void dl_note_size(HDC dc, int h, int *w, int *bh);
void dl_draw_note(HDC dc, int x, int y, int h);
// "Achievement unlocked" over the game (dl_achieve.c): the name being shown or NULL, its box, its drawing
const wchar_t *dl_toast_now(void);
void dl_toast_size(HDC dc, int h, int *w, int *bh);
void dl_draw_toast(HDC dc, int x, int y, int h);
void dl_toast_sample(void); // --gpu-check: the first achievement's box, without unlocking it
HBITMAP dl_picture_load(const char *path, int *w, int *h);

// the program's own list pages (DlList: dl.h)
extern const DlList dl_backups_list; // dl_saves.c: the copies kept before the saves were written over
extern const DlList dl_saves_list, dl_shots_list;
void dl_draw_picture(HDC dc, HBITMAP bmp, int pw, int ph, double x0, double y0, double x1, double y1); // whole, centred
void dl_draw_pixels(HDC dc, const u32 *px, int pw, int ph, double aspect, double x0, double y0, double x1, double y1);

// ---- what the setup screen shows (dl_menu.c)
enum { M_GAME, M_SIDE, M_PAGE, M_TRY, M_PAUSE };
bool dl_ahead_ok(void);  // the game can be run ahead (DlGame.run_ahead and its state in memory)
extern int dl_mode;  // M_SIDE: the navigation bar is in hand; M_PAGE: a page
extern int dl_side;  // entry of the navigation bar
extern int dl_cur;   // page shown
extern int dl_sel;   // row of the page (M_PAGE)
extern bool dl_dirty;

#define DL_MAX_PAGES 32
#define DL_MAX_ROWS 40
typedef struct {
  const wchar_t *const *tab;
  const DlRow *rows[DL_MAX_ROWS];
  int n;
  const DlList *list; // or a list instead of rows
  int top;            // list: first row shown
} DlPage;
extern DlPage dl_pages[DL_MAX_PAGES];
extern int dl_page_n;

enum { SEC_PAGE, SEC_PLAY, SEC_FOLDER, SEC_DEFAULTS, SEC_RESET, SEC_QUIT };
typedef struct {
  int kind, page;
  unsigned icon;
  const wchar_t *const *label, *const *help;
} DlSection;
#define DL_MAX_SECTIONS 16
extern DlSection dl_secs[DL_MAX_SECTIONS];
extern int dl_sec_n, dl_sec_top; // entries before dl_sec_top: upper group of the bar

// the kit's pages, in this order (DL_PAGE_LANGS, DL_PAGE_IMAGE, DL_PAGE_SOUND first)
enum { PG_CTRL = DL_PAGE_SOUND + 1, PG_KEYS, PG_PAD, PG_PADFN, PG_HOT, PG_SAVES, PG_SHOTS, PG_A11Y, PG_BACKUPS, PG_NEWS, PG_WELCOME, PG_WELCOME_CTRL, PG_WELCOME_SCREEN, PG_NET, PG_SETTINGS };
void dl_page_insert(int page, int at, const DlRow *row);
void dl_menu_init(void);   // the kit's pages and the first entry, before the game's setup()
void dl_menu_finish(void); // the kit's entries after the game's ones
void dl_safe_page_open(void); // the safe mode page, at the start of a run after a crash
void dl_online_start(void);      // dl_app.c: the look for a newer version, at start (if the player wants it)
void dl_online_follow(void);     // from the main loop: Discord's status, the result of that look
const wchar_t *dl_update_text(void); // "Version 1.4 is out." when a newer version was found, else NULL
void dl_update_open(void);       // its page in the browser
void dl_discord_changed(void);   // the Discord switch moved
bool dl_news_open(void);       // the game's news, the first time its version starts; false: none to show
void dl_welcome_open(void);    // the first start's pages (language, controls, display)
extern int dl_news_seen;       // the version whose news were shown (a hash of DlGame.version)
void dl_report_copy(void);      // dl_app.c: the crash report into the clipboard
const char *dl_report_file(void); // where it was written ("" none)
extern u32 *dl_menu_bits;                         // the setup screen as last drawn (window size)
extern int dl_prev_x, dl_prev_y, dl_prev_w, dl_prev_h; // the live picture in it (dl_prev_w 0: none)
void dl_draw_menu(HDC wdc);
void dl_draw_preview(HDC wdc);
void dl_key(int vk);
void dl_menu_reread(void); // list pages read their contents again when next shown
void dl_mouse(int x, int y, bool click);
int dl_audit(void);

// ---- graphics card output (dl_gpu.c); GDI does everything when it is off or fails
extern int dl_layer;              // the setup screen is being drawn as a layer: 1 on black, 2 on white
extern char dl_shot_path[MAX_PATH]; // set: the next dl_gpu_show saves what it drew there (dl_shot_ok)
extern bool dl_shot_ok, dl_shot_game; // dl_shot_game: only the game's picture, as soon as it is drawn
void dl_gpu_prepare(void);        // starts preparing the shaders in the background (once)
bool dl_gpu_ready(void);          // the graphics card is chosen and works (false while the shaders are prepared)
bool dl_gpu_show(bool rebuild, bool vsync);
int dl_gpu_steps(void);           // screen refreshes for each picture of the game played (smooth motion)
void dl_gpu_restart(void);        // after a change of the Display setting or of the kind of output
void dl_gpu_stop(void);
void dl_gpu_follow(bool now);     // HDR on or off as Windows shows the window's screen (each second)
bool dl_windows_hdr(void);
const wchar_t *dl_hdr_value(int v);

// ---- texts (dl_text.c): each is a DlText
enum {
  T_FOOTER, T_BACK, T_H_BACK, T_YES, T_NO, T_EMPTY,
  T_PLAY, T_H_PLAY, T_H_RESUME, T_PREVIEW,
  T_LANGS, T_TAB_LANGS, T_H_LANGS, T_UILANG, T_H_UILANG,
  T_IMAGE, T_TAB_IMAGE, T_H_IMAGE, T_FULL, T_H_FULL, T_INT, T_H_INT, T_FILTER, T_H_FILTER, T_SHARP, T_SMOOTH,
  T_SCAN, T_H_SCAN, T_FPS, T_H_FPS, T_TRY, T_H_TRY, T_TRY_BACK, T_GFXDEF, T_H_GFXDEF, T_EDGES,
  T_GPU, T_H_GPU, T_GPU_ON, T_GPU_OFF, T_CRT, T_H_CRT, T_CRT_FLAT, T_CRT_CURVED, T_HDR, T_H_HDR, T_HDR_WIN_ON, T_HDR_WIN_OFF,
  T_VSYNC, T_H_VSYNC,
  // controls
  T_CTRL, T_TAB_CTRL, T_H_CTRL, T_KEYS, T_H_KEYS, T_PAD, T_H_PAD, T_HOT, T_H_HOT, T_TAB_KEYS, T_TAB_PAD, T_TAB_PADFN, T_TAB_HOT, T_PADFN,
  T_BTN_UP, T_BTN_DOWN, T_BTN_LEFT, T_BTN_RIGHT, T_BTN_A, T_BTN_B, T_BTN_X, T_BTN_Y, T_BTN_L, T_BTN_R, T_BTN_START, T_BTN_SELECT, T_BTN_TURBO,
  T_PH_WAIT, T_PH_TURBO, T_PH_BTN, T_PH_DEFAULTS, T_PH_FN, T_PH_ACT, T_FN_MENU, T_KH_WAIT, T_KH_TURBO, T_KH_BTN, T_KH_DEFAULTS,
  T_PAD_UP, T_PAD_DOWN, T_PAD_LEFT, T_PAD_RIGHT, T_PAD_LCLICK, T_PAD_RCLICK, T_NONE, T_DPAD_STICK, T_BUTTON_N, T_KEY_N,
  T_HK_SAVE, T_HK_LOAD, T_HK_PREV, T_HK_NEXT, T_HK_SHOT, T_HK_FULL, T_HKH,
  T_TURBO, T_H_TURBO, T_MAX, T_PAUSEMENU, T_H_PAUSEMENU, T_BGPAUSE, T_H_BGPAUSE,
  // save states, screenshots, pause menu, notes over the game
  T_SAVES, T_TAB_SAVES, T_H_SAVES, T_SLOT_N, T_SVH_LOAD, T_SVH_SAVE, T_SVH_EMPTY, T_SVH_CONFIRM,
  T_SHOTS, T_TAB_SHOTS, T_H_SHOTS, T_SHH_SIZE, T_SHH_OPEN, T_SHH_CONFIRM, T_SHH_NONE,
  T_TAB_PAUSE, T_RESUME, T_PA_SETTINGS, T_PAH,
  T_M_SAVED, T_M_SAVE_FAIL, T_M_LOADED, T_M_LOAD_FAIL, T_M_SLOT_EMPTY, T_M_SLOT, T_M_SLOT_FREE, T_M_SHOT, T_M_SHOT_FAIL,
  T_CONTINUE, T_H_CONTINUE, T_SESSION, T_SESSION_DATE, T_SVH_F2, T_SVH_NAME, T_HK_REWIND, T_SVH_AUTO, T_SVH_AUTO_EMPTY, T_M_REWIND, T_M_RESUMED,
  T_SOUND, T_TAB_SOUND, T_H_SOUND, T_VOL, T_H_VOL, T_MENUSND, T_H_MENUSND,
  T_FOLDER, T_H_FOLDER, T_DEFAULTS, T_H_DEFAULTS, T_RESET, T_H_RESET, T_QUIT, T_H_QUIT,
  T_FT_MOVE, T_FT_OK, T_FT_BACK, T_PH_DEFAULTS_GAME, T_PH_MENU_HOLD,
  T_RUMBLE, T_H_RUMBLE, T_PS_NOTE, T_PS_CROSS, T_PS_CIRCLE, T_PS_SQUARE, T_PS_TRIANGLE, T_PS_TOUCH,
  T_A11Y, T_TAB_A11Y, T_H_A11Y, T_CONTRAST, T_H_CONTRAST, T_CVD, T_H_CVD, T_CVD_PROTAN, T_CVD_DEUTAN, T_CVD_TRITAN, T_STILL, T_H_STILL,
  T_BIG_HELP, T_H_BIG_HELP,
  T_PK_ON, T_PK_OFF, T_PK_FOLDER, T_H_PK_FOLDER, T_PK_RESTART, T_H_PK_RESTART, T_H_PK, T_PK_DIR, T_PK_FILE,
  T_ACH, T_TAB_ACH, T_H_ACH, T_ACH_SECRET, T_ACH_H_SECRET, T_ACH_UNLOCKED, T_ACH_COUNT, T_ACH_LOCKED, T_ACH_DATE,
  T_SAFE_TAB, T_SAFE_KEEP, T_H_SAFE_KEEP, T_H_SAFE_KEEP_HDR, T_H_SAFE_KEEP_CRT, T_H_SAFE_KEEP_SOFT, T_SAFE_LOAD, T_H_SAFE_LOAD, T_SAFE_RESTART, T_H_SAFE_RESTART,
  T_AHEAD, T_H_AHEAD, T_AHEAD_1, T_AHEAD_2,
  T_SAFE_REPORT, T_H_SAFE_REPORT, T_M_REPORT,
  T_SVH_X, T_SVH_OSK,
  T_H_SETTINGS, T_TAB_SETTINGS,
  T_NET, T_H_NET, T_TAB_NET, T_DISCORD, T_H_DISCORD, T_DS_PLAYING, T_DS_MENUS, T_UPDATES, T_H_UPDATES, T_UPD_NEW, T_UPD_NONE, T_UPD_OPEN, T_H_UPD_OPEN,
  T_TAB_NEWS, T_H_NEWS, T_TAB_WELCOME, T_W_NEXT, T_H_W_LANG, T_H_W_CTRL, T_W_PLAY, T_H_W_PLAY,
  T_H_CTRL_NT, T_H_KEYS_NT, T_PH_FN_NT, T_KH_DEFAULTS_NT, // without fast forward (DL_HIDE_TURBO)
  T_PRESET, T_H_PRESET, T_H_PRESET_NOCRT, T_PRE_CUSTOM, T_PRE_ORIGINAL, T_PRE_LARGE, T_PRE_CRT, T_PRE_FAST,
  T_BACKUPS, T_H_BACKUPS, T_TAB_BACKUPS, T_BKH, T_BKH_GAME, T_BKH_CONFIRM, T_BK_NONE, T_BK_SESSION, T_BK_RESTORED,
  T_COUNT
};
extern const wchar_t *const dl_texts[T_COUNT][DL_LANGS];
extern const wchar_t *const dl_lang_names[DL_LANGS];
#define TX(id) (dl_tr(dl_texts[id]))
#define DL_WIDEN2(s) L##s
#define DL_WIDEN(s) DL_WIDEN2(s)
#define DL_SEP_W DL_WIDEN(DL_SEP)
bool dl_lang_ok(int lang); // one of the game's languages (DlGame.langs)
int dl_lang_default(void); // the language of Windows if the game has it, otherwise English (or its first)

// ---- drawing on the logical 640 x 400 canvas (dl_draw.c)
extern double ui_s;
extern int ui_ox, ui_oy;
extern HFONT f_item, f_small, f_help, f_help_small;
extern int dl_audit_issues;
int PX(double v);
void ui_fonts(void);
void ui_audit_note(const char *what, const wchar_t *text);
void ui_rect(HDC dc, double x0, double y0, double x1, double y1, COLORREF c);
void ui_rect_alpha(HDC dc, double x0, double y0, double x1, double y1, COLORREF c, int alpha);
void ui_round(HDC dc, double x0, double y0, double x1, double y1, COLORREF c, double r);
void ui_panel(HDC dc, double x0, double y0, double x1, double y1, COLORREF fill, const wchar_t *tab);
void ui_text(HDC dc, HFONT f, double x, double y, const wchar_t *s, COLORREF c, UINT align);
void ui_text_fit(HDC dc, double x, double y, const wchar_t *s, COLORREF c, UINT align, double max_w);
void ui_title(HDC dc, double x, double base);
void ui_bar(HDC dc, double x0, double y, double x1, int value, int max);
void ui_row(HDC dc, double y, double h, bool sel, const wchar_t *label, const wchar_t *value, int sw, bool plain, int bar, int bar_max);
HFONT ui_icons(HDC dc);
void ui_pad_glyph(HDC dc, int glyph, double cx, double cy, double d); // a controller button, d across, centred at cx, cy
void sky_draw(HDC dc, int cw, int ch);
bool backdrop_draw(HDC dc, int cw, int ch); // the player's launcher_bg.*, or the theme's backdrop; false: none

// ---- the game's theme (DlTheme, dl_draw.c)
COLORREF ui_tint(COLORREF c); // one of DarkLauncher's colours in the theme's hue and strength (c itself without one)
COLORREF ui_value(void);      // the colour of values and bars
bool ui_theme_hsl(float *turn, float *strength); // the theme's colour as dl_gpu.c's sky needs it; false: none
int ui_sky_style(void);       // DL_SKY_*
void ui_box(HDC dc, double x0, double y0, double x1, double y1, COLORREF c, double r); // ui_round in the theme's frame style
void ui_cursor(HDC dc, double x0, double y0, double x1, double y1);                   // the selected row
bool ui_theme_icon(HDC dc, unsigned icon, double x0, double y0, double x1, double y1); // false: the theme has none
void ui_window_icon(void);    // the theme's icon 0 on the window, if any
// The theme's sound for dl_beep (kind 0 moving, 1 choosing) as a WAV in memory at that volume (0-10); NULL: none.
const void *ui_theme_sound(int kind, int volume);

#define COL_PANEL ui_tint(RGB(12, 30, 84))
#define COL_SIDE ui_tint(RGB(6, 18, 58))
#define COL_EDGE ui_tint(RGB(96, 140, 220))
#define COL_SEL RGB(255, 255, 255)
#define COL_VALUE ui_value()

// layout of the canvas
#define ROW_H 20.0
#define ROW_Y0 70.0
#define ROW_X0 167.0
#define ROW_X1 425.0
#define VAL_W 108.0
#define VAL_X0 (ROW_X1 - 5 - VAL_W)
#define VAL_X1 (ROW_X1 - 5)
#define SIDE_X0 13.0
#define SIDE_X1 153.0
#define PANEL_BOTTOM 372.0

// The CRT filter and the scanlines as they are applied: 0 when the game hides their row (DlGame.hide).
int dl_crt(void);
int dl_scanlines(void);

#endif
