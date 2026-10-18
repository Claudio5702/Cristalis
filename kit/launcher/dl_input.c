// Controls, taken from Kahna: the keyboard keys and the controller buttons of the game's buttons, the
// controller buttons and keyboard shortcuts of the program's own functions (fast forward, save states,
// screenshot, menu, fullscreen), all of them assignable in the Controls pages; the controller in the
// setup screen and the pause menu.
//
// Controllers: Xbox-type pads through XInput (every connected pad merged into one), others (PlayStation
// and generic pads) through the WinMM joystick functions, with buttons of their own.
#include "dl_int.h"
#include <mmsystem.h>
#include <xinput.h>

int dl_key_vk[DL_BTNS + 1], dl_pad_code[FN_ALL], dl_joy_code[FN_ALL], dl_hot_vk[HK_COUNT];
DlFunc dl_funcs[DL_MAX_FUNCS];
int dl_func_n;
int *dl_wait;
static bool wait_pad;      // dl_wait is a controller button
static bool wait_release;  // the game gets no button until every one is released (after a menu)

// ---- defaults
// keyboard: arrows, then by position (the same on QWERTY and AZERTY keyboards) X A, Z B, S X, A Y,
// Q L, W R; Enter Start, right Shift Select; Tab fast forward
void dl_keys_default(void) {
  static const int scancodes[DL_BTNS] = {0, 0, 0, 0, 0x2D, 0x2C, 0x1F, 0x1E, 0x10, 0x11, 0, 0};
  static const int fixed[DL_BTNS] = {VK_UP, VK_DOWN, VK_LEFT, VK_RIGHT, 0, 0, 0, 0, 0, 0, VK_RETURN, VK_RSHIFT};
  for (int k = 0; k < DL_BTNS; k++) dl_key_vk[k] = scancodes[k] ? (int)MapVirtualKeyW((UINT)scancodes[k], MAPVK_VSC_TO_VK) : fixed[k];
  dl_key_vk[DL_BTNS] = VK_TAB;
  dl_dirty = true;
}

// controllers: the game's buttons in the places of a Super Nintendo pad's (its A is the right face
// button, its B the bottom one), LB/RB for L/R; View opens the menu
// (the game's functions have no controller button at first)
static const int pad_def[FN_ALL] = {0, 1, 2, 3, 13, 12, 15, 14, 8, 9, 4, 5, -1, -1, -1, -1, 5, -1, -1, -1, -1, -1, -1, -1, -1};
static const int joy_def[FN_ALL] = {-1, -1, -1, -1, 2, 1, 3, 0, 4, 5, 9, 8, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1};

void dl_pads_default(void) {
  if (dl_joy_mode()) memcpy(dl_joy_code, joy_def, sizeof joy_def);
  else memcpy(dl_pad_code, pad_def, sizeof pad_def);
  dl_dirty = true;
}

void dl_hot_default(void) {
  static const int d[HK_COUNT] = {VK_F5, VK_F7, VK_F2, VK_F3, VK_F12, VK_F11, VK_BACK};
  memcpy(dl_hot_vk, d, sizeof d);
  for (int k = 0; k < dl_func_n; k++) dl_funcs[k].key = dl_funcs[k].def_key;
  dl_dirty = true;
}

void dl_input_init(void) {
  static const char *const key_names[DL_BTNS + 1] = {"key_up", "key_down", "key_left", "key_right", "key_a", "key_b", "key_x",
                                                     "key_y", "key_l", "key_r", "key_start", "key_select", "key_turbo"};
  // the names of Kahna's settings file, so that a port moving to the kit keeps its players' settings
  static const char *const pad_names[FN_COUNT] = {"pad_up", "pad_down", "pad_left", "pad_right", "pad_a", "pad_b", "pad_x", "pad_y", "pad_l",
                                                  "pad_r", "pad_start", "pad_select", "pad_turbo", "pad_fnsave", "pad_fnload", "pad_fnshot", "pad_fnmenu"};
  static const char *const joy_names[FN_COUNT] = {"joy_up", "joy_down", "joy_left", "joy_right", "joy_a", "joy_b", "joy_x", "joy_y", "joy_l",
                                                  "joy_r", "joy_start", "joy_select", "joy_turbo", "joy_fnsave", "joy_fnload", "joy_fnshot", "joy_fnmenu"};
  static const char *const hot_names[HK_COUNT] = {"hot_save", "hot_load", "hot_slot_prev", "hot_slot_next", "hot_capture", "hot_fullscreen", "hot_rewind"};
  dl_keys_default();
  memcpy(dl_pad_code, pad_def, sizeof pad_def);
  memcpy(dl_joy_code, joy_def, sizeof joy_def);
  dl_hot_default();
  // kept by "Defaults" (they have their own "Default values" rows)
  for (int k = 0; k <= DL_BTNS; k++) dl_setting(key_names[k], &dl_key_vk[k], 0, 255, true);
  for (int k = 0; k < FN_COUNT; k++) {
    dl_setting(pad_names[k], &dl_pad_code[k], -1, 17, true);
    dl_setting(joy_names[k], &dl_joy_code[k], -1, 31, true);
  }
  for (int k = 0; k < HK_COUNT; k++) dl_setting(hot_names[k], &dl_hot_vk[k], 0, 255, true);
}

// ---- the game's functions: a row on the Shortcuts page, a row on the controller's Functions page, a row
// of the pause menu
void dl_function(const wchar_t *const *label, const wchar_t *const *help, const char *hot, int key, const char *pad,
                 void (*run)(void), bool in_pause) {
  static DlRow key_rows[DL_MAX_FUNCS], pad_rows[DL_MAX_FUNCS];
  static char names[DL_MAX_FUNCS][2][40];
  if (dl_func_n >= DL_MAX_FUNCS || !run) return;
  int k = dl_func_n++;
  DlFunc *f = &dl_funcs[k];
  *f = (DlFunc){label, help, run, in_pause, hot ? key : 0, hot ? key : 0};
  if (hot) {
    dl_setting(hot, &f->key, 0, 255, true);
    key_rows[k] = (DlRow){.kind = DL_KEY, .label = label, .help = dl_texts[T_HKH], .value = &f->key};
    dl_page_insert(PG_HOT, dl_pages[PG_HOT].n - 1, &key_rows[k]); // before "Default values"
  }
  if (pad) {
    snprintf(names[k][0], sizeof names[k][0], "pad%s", pad);
    snprintf(names[k][1], sizeof names[k][1], "joy%s", pad);
    dl_setting(names[k][0], &dl_pad_code[FN_COUNT + k], -1, 17, true);
    dl_setting(names[k][1], &dl_joy_code[FN_COUNT + k], -1, 31, true);
    pad_rows[k] = (DlRow){.kind = DL_PADKEY, .label = label, .help = dl_texts[T_PH_ACT], .value = &dl_pad_code[FN_COUNT + k], .max = 1};
    dl_page_insert(PG_PADFN, dl_pages[PG_PADFN].n, &pad_rows[k]);
  }
}

// ---- reading the controllers
typedef DWORD(WINAPI *XInputGetStateFn)(DWORD, XINPUT_STATE *);
static XInputGetStateFn xinput_get;
static bool xinput_present, joy_present;
static u32 pad_raw;      // XInput codes held (bit n: code n), and the left stick:
#define STICK_UP (1u << 18)
#define STICK_DOWN (1u << 19)
#define STICK_LEFT (1u << 20)
#define STICK_RIGHT (1u << 21)
static DWORD joy_raw;    // WinMM buttons held
static bool joy_ps;      // that controller is a PlayStation one (by its name)
int dl_input_kind;

void dl_input_used(int kind) {
  if (kind == dl_input_kind) return;
  dl_input_kind = kind;
  dl_dirty = true;
}

// The button that chooses (or goes back) in the menus, as a picture: A and B of an Xbox pad; on another
// controller the buttons of the game's A and B (dl_pad_poll), drawn as PlayStation symbols when it is
// one (DirectInput order: square, cross, circle, triangle), as their numbers otherwise.
int dl_pad_glyph(bool confirm) {
  if (dl_input_kind == DL_IN_XBOX) return confirm ? GL_XA : GL_XB;
  int n = dl_joy_code[confirm ? 4 : 5];
  if (n < 0) n = confirm ? 1 : 0;
  if (dl_input_kind == DL_IN_PS && n < 4) return GL_PS_SQUARE + n;
  return GL_NUM + n;
}
static u32 joy_dirs;     // its stick and hat, as DL_UP..DL_RIGHT

static void xinput_load(void) {
  static bool tried;
  static const char *const dlls[] = {"xinput1_4.dll", "xinput1_3.dll", "xinput9_1_0.dll"};
  if (tried) return;
  tried = true;
  for (int k = 0; k < 3 && !xinput_get; k++) {
    HMODULE m = LoadLibraryA(dlls[k]);
    if (m) xinput_get = (XInputGetStateFn)(void *)GetProcAddress(m, "XInputGetState");
  }
}

// Every connected XInput pad merged into one, so it does not matter which slot the real one sits in;
// empty slots are probed again once a second only (probing them on every picture is slow).
static u32 xinput_poll(void) {
  static bool connected[4];
  static DWORD last_scan;
  u32 raw = 0;
  xinput_load();
  xinput_present = false;
  bool rescan = xinput_get && GetTickCount() - last_scan > 1000;
  if (rescan) last_scan = GetTickCount();
  for (DWORD slot = 0; slot < 4 && xinput_get; slot++) {
    XINPUT_STATE st;
    if (!connected[slot] && !rescan) continue;
    connected[slot] = xinput_get(slot, &st) == ERROR_SUCCESS;
    if (!connected[slot]) continue;
    xinput_present = true;
    raw |= st.Gamepad.wButtons;
    if (st.Gamepad.bLeftTrigger > 100) raw |= 1u << 16;
    if (st.Gamepad.bRightTrigger > 100) raw |= 1u << 17;
    if (st.Gamepad.sThumbLY > 16000) raw |= STICK_UP;
    if (st.Gamepad.sThumbLY < -16000) raw |= STICK_DOWN;
    if (st.Gamepad.sThumbLX < -16000) raw |= STICK_LEFT;
    if (st.Gamepad.sThumbLX > 16000) raw |= STICK_RIGHT;
  }
  return raw;
}

// The first joystick Windows reports (looked for again once a second while there is none).
static bool joy_poll(void) {
  static int id = -1;
  static DWORD last_scan;
  JOYINFOEX ji = {sizeof ji, JOY_RETURNALL};
  joy_present = false;
  joy_raw = joy_dirs = 0;
  if (id < 0) {
    if (GetTickCount() - last_scan < 1000) return false;
    last_scan = GetTickCount();
    UINT n = joyGetNumDevs();
    for (UINT k = 0; k < n && k < 16 && id < 0; k++)
      if (joyGetPosEx(k, &ji) == JOYERR_NOERROR) id = (int)k;
    if (id < 0) return false;
    JOYCAPSW caps;
    joy_ps = false;
    if (joyGetDevCapsW((UINT)id, &caps, sizeof caps) == JOYERR_NOERROR) {
      static const wchar_t *const ps_names[] = {L"Wireless Controller", L"DualSense", L"DUALSHOCK", L"PLAYSTATION", L"PS4", L"PS5"};
      CharUpperW(caps.szPname);
      for (int k = 0; k < 6 && !joy_ps; k++) {
        wchar_t want[32];
        lstrcpynW(want, ps_names[k], 32);
        CharUpperW(want);
        joy_ps = wcsstr(caps.szPname, want) != NULL;
      }
    }
  }
  if (joyGetPosEx((UINT)id, &ji) != JOYERR_NOERROR) { id = -1; return false; }
  if (ji.dwXpos < 16000) joy_dirs |= DL_LEFT;
  if (ji.dwXpos > 49000) joy_dirs |= DL_RIGHT;
  if (ji.dwYpos < 16000) joy_dirs |= DL_UP;
  if (ji.dwYpos > 49000) joy_dirs |= DL_DOWN;
  if ((ji.dwPOV & 0xFFFF) != 0xFFFF) {
    DWORD a = ji.dwPOV;
    if (a >= 29250 || a <= 6750) joy_dirs |= DL_UP;
    if (a >= 2250 && a <= 15750) joy_dirs |= DL_RIGHT;
    if (a >= 11250 && a <= 24750) joy_dirs |= DL_DOWN;
    if (a >= 20250 && a <= 33750) joy_dirs |= DL_LEFT;
  }
  joy_present = true;
  joy_raw = ji.dwButtons;
  return true;
}

// The controller pages edit the WinMM assignment when no Xbox-type pad is connected but another one is.
bool dl_joy_mode(void) { return !xinput_present && joy_present; }

static bool fn_held(int k) {
  if (dl_pad_code[k] >= 0 && (pad_raw & (1u << dl_pad_code[k]))) return true;
  return joy_present && dl_joy_code[k] >= 0 && (joy_raw & (1u << dl_joy_code[k]));
}

u32 dl_game_buttons(void) {
  u32 b = 0;
  if (GetForegroundWindow() == dl_hwnd)
    for (int k = 0; k < DL_BTNS; k++)
      if (dl_key_vk[k] && GetAsyncKeyState(dl_key_vk[k]) < 0) b |= 1u << k;
  for (int k = 0; k < DL_BTNS; k++)
    if (fn_held(k)) b |= 1u << k;
  // the left stick always doubles as the directional pad
  if (pad_raw & STICK_UP) b |= DL_UP;
  if (pad_raw & STICK_DOWN) b |= DL_DOWN;
  if (pad_raw & STICK_LEFT) b |= DL_LEFT;
  if (pad_raw & STICK_RIGHT) b |= DL_RIGHT;
  if (joy_present) b |= joy_dirs;
  if (wait_release) {
    if (b) return 0;
    wait_release = false;
  }
  return b;
}

void dl_input_release(void) { wait_release = true; }

bool dl_turbo_held(void) {
  if (GetForegroundWindow() == dl_hwnd && dl_key_vk[DL_BTNS] && GetAsyncKeyState(dl_key_vk[DL_BTNS]) < 0) return true;
  return fn_held(FN_TURBO);
}

// ---- assignment
void dl_wait_for(int *value, bool pad) {
  dl_wait = value;
  wait_pad = pad;
  dl_dirty = true;
}
bool dl_wait_pad(void) { return dl_wait && wait_pad; }

// A key pressed while the setup screen waits for one (Esc cancels).
void dl_wait_key(int vk) {
  if (!wait_pad && vk != VK_ESCAPE) *dl_wait = vk;
  if (!wait_pad || vk == VK_ESCAPE) dl_wait = NULL;
  dl_dirty = true;
}

// The value of a DL_PADKEY row: in WinMM mode, the kit's own rows stand for the WinMM assignment.
int *dl_pad_value(int *value) {
  if (value >= dl_pad_code && value < dl_pad_code + FN_ALL && dl_joy_mode()) return dl_joy_code + (value - dl_pad_code);
  return value;
}

// ---- names
const wchar_t *dl_key_name(int vk) {
  static wchar_t out[4][64];
  static int turn;
  wchar_t *o = out[turn++ & 3];
  UINT sc = MapVirtualKeyW((UINT)vk, MAPVK_VK_TO_VSC);
  LONG lp = (LONG)(sc << 16);
  switch (vk) {
    case VK_LEFT: case VK_RIGHT: case VK_UP: case VK_DOWN: case VK_INSERT: case VK_DELETE: case VK_HOME: case VK_END:
    case VK_PRIOR: case VK_NEXT: case VK_RCONTROL: case VK_RMENU: case VK_DIVIDE: case VK_RSHIFT:
      lp |= 1 << 24;
  }
  if (vk == VK_RSHIFT) lp = (LONG)(0x36 << 16); // the right Shift has a scan code of its own
  if (!vk) lstrcpyW(o, TX(T_NONE));
  else if (!sc || !GetKeyNameTextW(lp, o, 64)) wsprintfW(o, TX(T_KEY_N), vk);
  return o;
}

const wchar_t *dl_pad_name(int *value) {
  static const wchar_t *const names[18] = {NULL, NULL, NULL, NULL, L"Start", L"Back", NULL, NULL, L"LB", L"RB", L"?", L"?",
                                           L"A", L"B", L"X", L"Y", L"LT", L"RT"};
  static wchar_t out[4][32];
  static int turn;
  int *v = dl_pad_value(value), code = *v;
  if (v != value) { // WinMM: button numbers
    if (code < 0) return v - dl_joy_code < 4 ? TX(T_DPAD_STICK) : TX(T_NONE);
    wchar_t *o = out[turn++ & 3];
    wsprintfW(o, TX(T_BUTTON_N), code + 1);
    return o;
  }
  if (code >= 0 && code < 4) return TX(T_PAD_UP + code);
  if (code == 6 || code == 7) return TX(T_PAD_LCLICK + code - 6);
  return code >= 0 && code < 18 ? names[code] : TX(T_NONE);
}

// ---- each turn of the main loop: the controllers read once; in the setup screen they move in it
// (directions with auto-repeat, A or Start chooses, B goes back, Y erases), in the game they reach the
// program's functions; a button being waited for is assigned
void dl_pad_poll(void) {
  static u32 prev;
  static DWORD prev_joy, repeat_at;
  static bool held;
  pad_raw = xinput_poll();
  if (!xinput_present) joy_poll();
  else joy_present = false;
  u32 pressed = pad_raw & ~prev;
  DWORD jpressed = joy_raw & ~prev_joy;
  prev = pad_raw;
  prev_joy = joy_raw;
  if (dl_wait && wait_pad) { // the first control newly pressed (sticks are not assignable)
    if (dl_joy_mode()) {
      for (int n = 0; n < 32; n++)
        if (jpressed & (1u << n)) { *dl_pad_value(dl_wait) = n; dl_wait = NULL; dl_dirty = true; break; }
    } else {
      for (int code = 0; code <= 17; code++)
        if (pressed & (1u << code)) { *dl_wait = code; dl_wait = NULL; dl_dirty = true; break; }
    }
    return;
  }
  if (dl_wait) return; // a keyboard key is expected
  bool joy = dl_joy_mode();
  static u32 prev_dirs;
  u32 dirs = (pad_raw & 0x3C000Fu) | joy_dirs;
  if (pressed || jpressed || (dirs & ~prev_dirs)) dl_input_used(!joy ? DL_IN_XBOX : joy_ps ? DL_IN_PS : DL_IN_PAD); // the footer shows its buttons
  prev_dirs = dirs;
#define FN_PRESSED(k) ((dl_pad_code[k] >= 0 && (pressed & (1u << dl_pad_code[k]))) || (joy && dl_joy_code[k] >= 0 && (jpressed & (1u << dl_joy_code[k]))))
  if (dl_mode == M_GAME) {
    if (FN_PRESSED(FN_MENU)) dl_pause_open();
    else if (FN_PRESSED(FN_SAVE)) dl_quick_save();
    else if (FN_PRESSED(FN_LOAD)) dl_quick_load();
    else if (FN_PRESSED(FN_SHOT)) dl_screenshot();
    else
      for (int k = 0; k < dl_func_n; k++)
        if (FN_PRESSED(FN_COUNT + k)) { dl_funcs[k].run(); break; }
    return;
  }
  if (dl_mode == M_PAUSE && FN_PRESSED(FN_MENU)) { dl_key(VK_ESCAPE); return; } // the button that opens it closes it
  u32 up = (pad_raw & (1u | STICK_UP)) || (joy_dirs & DL_UP), down = (pad_raw & (2u | STICK_DOWN)) || (joy_dirs & DL_DOWN);
  u32 left = (pad_raw & (4u | STICK_LEFT)) || (joy_dirs & DL_LEFT), right = (pad_raw & (8u | STICK_RIGHT)) || (joy_dirs & DL_RIGHT);
  bool any = up || down || left || right;
  DWORD now = GetTickCount();
  bool fire = false;
  if (any && !held) { fire = true; repeat_at = now + 350; }
  else if (any && now >= repeat_at) { fire = true; repeat_at = now + 110; }
  held = any;
  if (fire) dl_key(up ? VK_UP : down ? VK_DOWN : left ? VK_LEFT : VK_RIGHT);
  if (pressed & ((1u << 12) | (1u << 4))) dl_key(VK_RETURN); // A or Start
  if (pressed & (1u << 13)) dl_key(VK_ESCAPE);               // B
  if (pressed & (1u << 15)) dl_key(VK_DELETE);               // Y
  if (pressed & (1u << 8)) dl_key(VK_PRIOR);                 // LB, RB: Page Up, Page Down (a pack moved)
  if (pressed & (1u << 9)) dl_key(VK_NEXT);
  if (joy) { // other controllers: the buttons of the game's A and B
    if (dl_joy_code[4] >= 0 && (jpressed & (1u << dl_joy_code[4]))) dl_key(VK_RETURN);
    if (dl_joy_code[5] >= 0 && (jpressed & (1u << dl_joy_code[5]))) dl_key(VK_ESCAPE);
    if (dl_joy_code[8] >= 0 && (jpressed & (1u << dl_joy_code[8]))) dl_key(VK_PRIOR);
    if (dl_joy_code[9] >= 0 && (jpressed & (1u << dl_joy_code[9]))) dl_key(VK_NEXT);
  }
#undef FN_PRESSED
}

// A key pressed while the game is played: the program's shortcuts. True when it was one.
bool dl_game_key(int vk) {
  if (vk == VK_ESCAPE) { dl_pause_open(); return true; }
  if (dl_game->save_state) {
    if (vk == dl_hot_vk[HK_SAVE]) { dl_quick_save(); return true; }
    if (vk == dl_hot_vk[HK_LOAD]) { dl_quick_load(); return true; }
    if (vk == dl_hot_vk[HK_PREV]) { dl_slot_step(-1); return true; }
    if (vk == dl_hot_vk[HK_NEXT]) { dl_slot_step(1); return true; }
  }
  if (vk == dl_hot_vk[HK_SHOT]) { dl_screenshot(); return true; }
  if (vk == dl_hot_vk[HK_FULL]) { dl_toggle_fullscreen(); return true; }
  for (int k = 0; k < dl_func_n; k++)
    if (dl_funcs[k].key && vk == dl_funcs[k].key) { dl_funcs[k].run(); return true; }
  return false;
}
