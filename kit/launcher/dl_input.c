// Controls, taken from Kahna: the keyboard keys and the controller buttons of the game's buttons, the
// controller buttons and keyboard shortcuts of the program's own functions (fast forward, save states,
// screenshot, menu, fullscreen), all of them assignable in the Controls pages; the controller in the
// setup screen and the pause menu.
//
// Controllers: Xbox-type pads through XInput, PlayStation ones read directly (dl_sony.c), others
// (generic pads) through the WinMM joystick functions, with buttons of their own. In the menus and for
// the program's functions every controller counts; in the game, one player has them all, two players
// (DlGame.players) share them: the first to player 1 (with the keyboard), the second to player 2.
#include "dl_int.h"
#include <math.h>
#include <mmsystem.h>
#include <stdlib.h>
#include <xinput.h>

int dl_key_vk[DL_BTNS + 1], dl_pad_code[FN_ALL], dl_joy_code[FN_ALL], dl_hot_vk[HK_COUNT];
DlFunc dl_funcs[DL_MAX_FUNCS];
int dl_func_n;
int *dl_wait;
static bool wait_pad;      // dl_wait is a controller button
static bool wait_release;  // the game gets no button until every one is released (after a menu)

// ---- defaults
// keyboard: arrows, then by position (the same on QWERTY and AZERTY keyboards) X A, Z B, S X, A Y,
// Q L, W R; Enter Start, right Shift Select; E L2, R R2, C L3, V R3 (DlGame.extra_buttons); Tab fast
// forward
void dl_keys_default(void) {
  static const int scancodes[DL_BTNS] = {0, 0, 0, 0, 0x2D, 0x2C, 0x1F, 0x1E, 0x10, 0x11, 0, 0, 0x12, 0x13, 0x2E, 0x2F};
  static const int fixed[DL_BTNS] = {VK_UP, VK_DOWN, VK_LEFT, VK_RIGHT, 0, 0, 0, 0, 0, 0, VK_RETURN, VK_RSHIFT, 0, 0, 0, 0};
  for (int k = 0; k < DL_BTNS; k++) dl_key_vk[k] = scancodes[k] ? (int)MapVirtualKeyW((UINT)scancodes[k], MAPVK_VSC_TO_VK) : fixed[k];
  dl_key_vk[DL_BTNS] = VK_TAB;
  dl_dirty = true;
}

// controllers: the game's buttons in the places of a Super Nintendo pad's (its A is the right face
// button, its B the bottom one), LB/RB for L/R, the triggers for L2/R2, the sticks' clicks for L3/R3;
// View opens the menu (the game's functions have no controller button at first)
// (the game may ask for the Xbox places, and for another menu button: see layout_defaults)
static int pad_def[FN_ALL] = {0, 1, 2, 3, 13, 12, 15, 14, 8, 9, 4, 5, 16, 17, 6, 7, -1, -1, -1, -1, 5, -1, -1, -1, -1, -1, -1, -1, -1};
static int joy_def[FN_ALL] = {-1, -1, -1, -1, 2, 1, 3, 0, 4, 5, 9, 8, 6, 7, 10, 11, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1};

// DlGame.pad_layout and pad_menu: the game's A B X Y in the Xbox places (XInput A B X Y; on other
// controllers, DirectInput order square cross circle triangle: cross, circle, square, triangle), and no
// button for the menu at first.
static void layout_defaults(void) {
  if (dl_game->pad_layout == DL_PAD_XBOX) {
    static const int xb[4] = {12, 13, 14, 15}, joy[4] = {1, 2, 0, 3};
    memcpy(pad_def + 4, xb, sizeof xb);
    memcpy(joy_def + 4, joy, sizeof joy);
  }
  if (dl_game->pad_menu == DL_MENU_NONE) pad_def[FN_MENU] = -1;
}

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
  layout_defaults();
  static const char *const key_names[DL_BTNS + 1] = {"key_up", "key_down", "key_left", "key_right", "key_a", "key_b", "key_x", "key_y", "key_l",
                                                     "key_r", "key_start", "key_select", "key_l2", "key_r2", "key_l3", "key_r3", "key_turbo"};
  // the names of Kahna's settings file, so that a port moving to the kit keeps its players' settings
  static const char *const pad_names[FN_COUNT] = {"pad_up", "pad_down", "pad_left", "pad_right", "pad_a", "pad_b", "pad_x", "pad_y", "pad_l",
                                                  "pad_r", "pad_start", "pad_select", "pad_l2", "pad_r2", "pad_l3", "pad_r3",
                                                  "pad_turbo", "pad_fnsave", "pad_fnload", "pad_fnshot", "pad_fnmenu"};
  static const char *const joy_names[FN_COUNT] = {"joy_up", "joy_down", "joy_left", "joy_right", "joy_a", "joy_b", "joy_x", "joy_y", "joy_l",
                                                  "joy_r", "joy_start", "joy_select", "joy_l2", "joy_r2", "joy_l3", "joy_r3",
                                                  "joy_turbo", "joy_fnsave", "joy_fnload", "joy_fnshot", "joy_fnmenu"};
  static const char *const hot_names[HK_COUNT] = {"hot_save", "hot_load", "hot_slot_prev", "hot_slot_next", "hot_capture", "hot_fullscreen", "hot_rewind"};
  dl_keys_default();
  memcpy(dl_pad_code, pad_def, sizeof pad_def);
  memcpy(dl_joy_code, joy_def, sizeof joy_def);
  dl_hot_default();
  // kept by "Defaults" (they have their own "Default values" rows); L2, R2, L3, R3 only for a game that
  // reads them (DlGame.extra_buttons): the others' settings files stay as they were
#define OFFERED(k) ((k) < DL_BTNS_CLASSIC || (k) >= DL_BTNS || (dl_game->extra_buttons & (1u << (k))))
  for (int k = 0; k <= DL_BTNS; k++)
    if (OFFERED(k)) dl_setting(key_names[k], &dl_key_vk[k], 0, 255, true);
  for (int k = 0; k < FN_COUNT; k++) {
    if (!OFFERED(k)) continue;
    dl_setting(pad_names[k], &dl_pad_code[k], -1, 17, true);
    dl_setting(joy_names[k], &dl_joy_code[k], -1, 31, true);
  }
#undef OFFERED
  if (dl_game->players >= 2) dl_setting("p2_pad", &dl_cfg.p2_pad, 0, 1, true);
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
typedef DWORD(WINAPI *XInputSetStateFn)(DWORD, XINPUT_VIBRATION *);
static XInputGetStateFn xinput_get;
static XInputSetStateFn xinput_set;
static bool xinput_present, joy_present, sony_present;
static bool xi_connected[4];
static u32 sony_raw;     // the PlayStation pads read directly, as XInput codes
static u32 pad_raw;      // XInput codes held (bit n: code n) by every controller, and the left stick:
#define STICK_UP (1u << 18)
#define STICK_DOWN (1u << 19)
#define STICK_LEFT (1u << 20)
#define STICK_RIGHT (1u << 21)
// each controller this turn, for the players: the XInput slots in their order, then the PlayStation
// pads read directly
#define PADS_MAX 8
static DlPadRead pads_now[PADS_MAX];
static int pads_n;
static DWORD joy_raw;    // WinMM buttons held
static DlPadRead joy_sticks; // its sticks (X Y, then Z and R: the right stick of most pads)
static u32 joy_hat;      // its hat, as DL_UP..DL_RIGHT
static bool joy_ps;      // that controller is a PlayStation one (by its name)
static bool joy_zr;      // it has the Z and R axes (a right stick)
int dl_input_kind;

static bool input_mouse; // the mouse used last in play (dl_mouse_used): DL_DEVICE_MOUSE, the footer the keyboard's

void dl_input_used(int kind) {
  if (dl_console) dl_pointer_hide(true); // a key or a controller: the pointer out of the way again
  input_mouse = false;
  if (kind == dl_input_kind) return;
  dl_input_kind = kind;
  dl_dirty = true;
}

void dl_mouse_used(void) {
  if (dl_input_kind != DL_IN_KEYS) dl_dirty = true;
  dl_input_kind = DL_IN_KEYS;
  input_mouse = true;
}

// The button that chooses (or goes back) in the menus, as a picture: A and B of an Xbox pad; on another
// controller the buttons of the game's A and B (dl_pad_poll), drawn as PlayStation symbols when it is
// one (DirectInput order: square, cross, circle, triangle), as their numbers otherwise.
int dl_pad_glyph(bool confirm) {
  if (dl_input_kind == DL_IN_XBOX) return confirm ? GL_XA : GL_XB;
  if (dl_input_kind == DL_IN_PS_HID) return confirm ? GL_PS_CROSS : GL_PS_CIRCLE; // as XInput's A and B
  int n = dl_joy_code[confirm ? 4 : 5];
  if (n < 0) n = confirm ? 1 : 0;
  if (dl_input_kind == DL_IN_PS && n < 4) return GL_PS_SQUARE + n;
  return GL_NUM + n;
}

// ---- the game's own button prompts (2.94)
int dl_input_device(void) {
  switch (dl_input_kind) {
    case DL_IN_XBOX: return DL_DEVICE_XBOX;
    case DL_IN_PS: case DL_IN_PS_HID: return DL_DEVICE_PLAYSTATION;
    case DL_IN_PAD: return DL_DEVICE_OTHER;
  }
  return input_mouse ? DL_DEVICE_MOUSE : DL_DEVICE_KEYBOARD;
}

// Changes when what dl_button_picture draws would: the device, an assignment, the WinMM mode.
unsigned dl_input_serial(void) {
  static u32 last;
  static unsigned serial = 1;
  u32 h = 2166136261u;
  const int *parts[3] = {dl_key_vk, dl_pad_code, dl_joy_code};
  const int counts[3] = {DL_BTNS, DL_BTNS, DL_BTNS};
  for (int p = 0; p < 3; p++)
    for (int k = 0; k < counts[p]; k++) h = (h ^ (u32)parts[p][k]) * 16777619u;
  h = (h ^ (u32)(dl_input_device() * 4 + dl_joy_mode() * 2 + joy_ps)) * 16777619u;
  if (h != last) last = h, serial++;
  return serial;
}

// What a game's button (bit k of DlInput.buttons) looks like on the device used now.
static DlButtonLook button_look(int k) {
  static const wchar_t *const arrows[4] = {L"\u2191", L"\u2193", L"\u2190", L"\u2192"};
  int device = dl_input_device();
  if (device == DL_DEVICE_MOUSE && (k == 4 || k == 5)) return (DlButtonLook){BP_MOUSE, k - 4, NULL};
  if (device == DL_DEVICE_KEYBOARD || device == DL_DEVICE_MOUSE) {
    int vk = dl_key_vk[k];
    if (!vk) return (DlButtonLook){BP_NONE, 0, NULL};
    bool arrow = vk >= VK_LEFT && vk <= VK_DOWN; // VK_LEFT, VK_UP, VK_RIGHT, VK_DOWN
    static const int order[4] = {2, 0, 3, 1};
    return (DlButtonLook){BP_KEY, 0, arrow ? arrows[order[vk - VK_LEFT]] : dl_key_name(vk)};
  }
  if (dl_joy_mode()) { // WinMM: the button's number, or a PlayStation symbol (DirectInput order)
    static wchar_t num[DL_BTNS][8];
    int n = dl_joy_code[k];
    if (n < 0) return k < 4 ? (DlButtonLook){BP_DPAD, k, NULL} : (DlButtonLook){BP_NONE, 0, NULL};
    if (joy_ps && n < 4) return (DlButtonLook){BP_PS, n, NULL};
    wsprintfW(num[k], L"%d", n + 1);
    return (DlButtonLook){BP_DISC, 0, num[k]};
  }
  int code = dl_pad_code[k]; // XInput codes (PlayStation pads read directly too)
  if (code < 0) return (DlButtonLook){BP_NONE, 0, NULL};
  if (code < 4) return (DlButtonLook){BP_DPAD, code, NULL};
  bool ps = device == DL_DEVICE_PLAYSTATION;
  if (code >= 12 && code <= 15) {
    static const int ps_of[4] = {1, 2, 0, 3}; // A cross, B circle, X square, Y triangle
    return ps ? (DlButtonLook){BP_PS, ps_of[code - 12], NULL} : (DlButtonLook){BP_XBOX, code - 12, NULL};
  }
  static const wchar_t *const xbox[18] = {NULL, NULL, NULL, NULL, L"Start", L"Back", L"LS", L"RS", L"LB", L"RB", L"?", L"?", NULL, NULL, NULL, NULL, L"LT", L"RT"};
  static const wchar_t *const sony[18] = {NULL, NULL, NULL, NULL, L"Options", L"Share", L"L3", L"R3", L"L1", L"R1", L"PS", L"Touch", NULL, NULL, NULL, NULL, L"L2", L"R2"};
  const wchar_t *label = code < 18 ? (ps ? sony : xbox)[code] : NULL;
  return label ? (DlButtonLook){BP_PILL, 0, label} : (DlButtonLook){BP_NONE, 0, NULL};
}

// Kept per button and size until the serial changes.
const uint32_t *dl_button_picture(unsigned button, int size, int *w, int *h) {
  static struct { int k, size, w, h; unsigned serial; u32 *px; } cache[32];
  if (w) *w = 0;
  if (h) *h = 0;
  if (!button || (button & (button - 1)) || button >= 1u << DL_BTNS) return NULL;
  int k = 0;
  while (!(button & (1u << k))) k++;
  size = size < 8 ? 8 : size > 256 ? 256 : size;
  unsigned serial = dl_input_serial();
  int slot = -1;
  for (int n = 0; n < 32 && slot < 0; n++)
    if (cache[n].px && cache[n].k == k && cache[n].size == size) slot = n;
  if (slot >= 0 && cache[slot].serial != serial) { free(cache[slot].px); cache[slot].px = NULL; }
  if (slot < 0 || !cache[slot].px) {
    if (slot < 0) {
      static int next;
      for (int n = 0; n < 32 && slot < 0; n++)
        if (!cache[n].px) slot = n;
      if (slot < 0) { slot = next++ & 31; free(cache[slot].px); cache[slot].px = NULL; } // (a game asking for many sizes)
    }
    DlButtonLook look = button_look(k);
    if (look.shape == BP_NONE) return NULL;
    cache[slot].px = ui_button_draw(look, size, &cache[slot].w, &cache[slot].h);
    cache[slot].k = k, cache[slot].size = size, cache[slot].serial = serial;
    if (!cache[slot].px) return NULL;
  }
  if (w) *w = cache[slot].w;
  if (h) *h = cache[slot].h;
  return cache[slot].px;
}

// --ui-audit: the pictures of every button the game offers, on each device, at three sizes, over a
// grey (the WinMM controllers stood for by the flags dl_joy_mode reads); a button without one is an issue.
void dl_buttons_audit(const char *dir) {
  static const char *const names[] = {"keys", "mouse", "xbox", "ps", "ps_winmm", "pad_winmm"};
  static const int sizes[3] = {24, 32, 64};
  int keep_kind = dl_input_kind;
  bool keep_mouse = input_mouse, keep_xi = xinput_present, keep_joy = joy_present, keep_ps = joy_ps;
  for (int dev = 0; dev < 6; dev++) {
    static const int kinds[6] = {DL_IN_KEYS, DL_IN_KEYS, DL_IN_XBOX, DL_IN_PS_HID, DL_IN_PS, DL_IN_PAD};
    dl_input_kind = kinds[dev];
    input_mouse = dev == 1;
    bool winmm = dev >= 4;
    xinput_present = !winmm, joy_present = winmm, joy_ps = dev == 4;
    int sw = 8, sh = 8;
    for (int z = 0; z < 3; z++) {
      int row = 8;
      for (int k = 0; k < DL_BTNS; k++) {
        if (k >= DL_BTNS_CLASSIC && !(dl_game->extra_buttons & (1u << k))) continue;
        int w, h;
        if (dl_button_picture(1u << k, sizes[z], &w, &h)) row += w + 6;
        else if (!z) {
          wchar_t what[64];
          wsprintfW(what, L"%hs, button %d", names[dev], k);
          ui_audit_note("button without picture", what);
        }
      }
      if (row > sw) sw = row;
      sh += sizes[z] + 8;
    }
    u32 *sheet = malloc((size_t)sw * sh * 4);
    if (!sheet) continue;
    for (int n = 0; n < sw * sh; n++) sheet[n] = 0xFF3C4254;
    int y = 8;
    for (int z = 0; z < 3; z++) {
      int x = 8;
      for (int k = 0; k < DL_BTNS; k++) {
        if (k >= DL_BTNS_CLASSIC && !(dl_game->extra_buttons & (1u << k))) continue;
        int w, h;
        const u32 *px = dl_button_picture(1u << k, sizes[z], &w, &h);
        if (!px) continue;
        for (int j = 0; j < h; j++)
          for (int i = 0; i < w; i++) { // the picture over the grey
            u32 p = px[j * w + i], a = p >> 24, *d = &sheet[(size_t)(y + j) * sw + x + i];
            u32 out = 0xFF000000;
            for (int c = 0; c < 24; c += 8) out |= (((p >> c) & 255) + ((*d >> c) & 255) * (255 - a) / 255) << c;
            *d = out;
          }
        x += w + 6;
      }
      y += sizes[z] + 8;
    }
    char path[MAX_PATH];
    snprintf(path, sizeof path, "%s" DL_SEP "buttons_%s.png", dir, names[dev]);
    dl_save_png(path, sheet, sw, sh);
    free(sheet);
  }
  dl_input_kind = keep_kind;
  input_mouse = keep_mouse, xinput_present = keep_xi, joy_present = keep_joy, joy_ps = keep_ps;
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
    if (m && xinput_get) xinput_set = (XInputSetStateFn)(void *)GetProcAddress(m, "XInputSetState");
  }
}

// Every connected XInput pad merged into one, so it does not matter which slot the real one sits in
// (each one also in pads_now, for the players); empty slots are probed again once a second only
// (probing them on every picture is slow).
static u32 xinput_poll(void) {
  bool *connected = xi_connected;
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
    const XINPUT_GAMEPAD *g = &st.Gamepad;
    u32 r = g->wButtons;
    if (g->bLeftTrigger > 100) r |= 1u << 16;
    if (g->bRightTrigger > 100) r |= 1u << 17;
    if (g->sThumbLY > 16000) r |= STICK_UP;
    if (g->sThumbLY < -16000) r |= STICK_DOWN;
    if (g->sThumbLX < -16000) r |= STICK_LEFT;
    if (g->sThumbLX > 16000) r |= STICK_RIGHT;
    raw |= r;
#ifdef _WIN32
    int id = (int)slot;
#else
    int id = dl_xinput_id(slot);
#endif
    if (pads_n < PADS_MAX) pads_now[pads_n++] = (DlPadRead){r, g->sThumbLX, g->sThumbLY, g->sThumbRX, g->sThumbRY, id};
  }
  return raw;
}

static s16 joy_axis(DWORD v, bool up) { // WinMM's 0-65535 (32767 in the middle) as XInput's
  int a = up ? 32767 - (int)v : (int)v - 32767;
  return (s16)(a > 32767 ? 32767 : a < -32767 ? -32767 : a);
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
    joy_ps = joy_zr = false;
    if (joyGetDevCapsW((UINT)id, &caps, sizeof caps) == JOYERR_NOERROR) {
      joy_zr = (caps.wCaps & JOYCAPS_HASZ) && (caps.wCaps & JOYCAPS_HASR); // a right stick
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
  joy_hat = 0;
  if ((ji.dwPOV & 0xFFFF) != 0xFFFF) {
    DWORD a = ji.dwPOV;
    if (a >= 29250 || a <= 6750) joy_hat |= DL_UP;
    if (a >= 2250 && a <= 15750) joy_hat |= DL_RIGHT;
    if (a >= 11250 && a <= 24750) joy_hat |= DL_DOWN;
    if (a >= 20250 && a <= 33750) joy_hat |= DL_LEFT;
  }
  joy_dirs |= joy_hat;
  joy_sticks = (DlPadRead){0, joy_axis(ji.dwXpos, false), joy_axis(ji.dwYpos, true), joy_zr ? joy_axis(ji.dwZpos, false) : 0, joy_zr ? joy_axis(ji.dwRpos, true) : 0, 32 + id};
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

// The game's buttons a controller holds: XInput codes (raw), or WinMM buttons (joy). Without
// DlGame.analog, the left stick doubles as the directional pad.
static u32 pad_buttons(u32 raw, bool joy, u32 joy_stick) {
  u32 b = 0;
  for (int k = 0; k < DL_BTNS; k++) {
    int code = joy ? dl_joy_code[k] : dl_pad_code[k];
    if (code >= 0 && (raw & (1u << code))) b |= 1u << k;
  }
  if (!dl_game->analog) {
    if (joy) b |= joy_stick;
    else {
      if (raw & STICK_UP) b |= DL_UP;
      if (raw & STICK_DOWN) b |= DL_DOWN;
      if (raw & STICK_LEFT) b |= DL_LEFT;
      if (raw & STICK_RIGHT) b |= DL_RIGHT;
    }
  }
  return b;
}

// A stick through XInput's dead zone (its own for the left stick and the right one): nothing within it,
// the rest stretched to the whole range, the same direction.
static void stick_dead(s16 *x, s16 *y, double dz) {
  double m = sqrt((double)*x * *x + (double)*y * *y);
  if (m <= dz) { *x = *y = 0; return; }
  double s = ((m > 32767 ? 32767 : m) - dz) / (32767 - dz) / m * 32767;
  *x = (s16)fmax(-32767, fmin(32767, *x * s));
  *y = (s16)fmax(-32767, fmin(32767, *y * s));
}

static void sticks_take(DlInput *in, const DlPadRead *p) { // the controller pushed furthest gives the player's sticks
  DlPadRead v = *p;
  stick_dead(&v.lx, &v.ly, 7849); // XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE
  stick_dead(&v.rx, &v.ry, 8689); // XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE
  if (abs(v.lx) + abs(v.ly) >= abs(in->lx) + abs(in->ly)) in->lx = v.lx, in->ly = v.ly;
  if (abs(v.rx) + abs(v.ry) >= abs(in->rx) + abs(in->ry)) in->rx = v.rx, in->ry = v.ry;
}

// Two players (DlGame.players 2): each one's controller by its id (-1: none yet). The first controller
// used takes player 1's place, the next one player 2's; with p2_pad, the first one used is player 2's
// (player 1 keeps the keyboard) and no other plays. A controller that goes gives its place back, and one
// never used plays for no one: a device that never presses anything, which Windows may count before the
// real pad, takes nobody's place.
static int seat[2] = {-1, -1};
static void seats_follow(const int *ids, const u32 *used, int n, bool p2_first) {
  for (int s = 0; s < 2; s++) {
    bool here = false;
    for (int i = 0; i < n && !here; i++) here = ids[i] == seat[s];
    if (!here) seat[s] = -1;
  }
  for (int i = 0; i < n; i++) {
    if (!used[i] || ids[i] == seat[0] || ids[i] == seat[1]) continue;
    int s = p2_first || seat[0] >= 0 ? 1 : 0;
    if (seat[s] < 0) seat[s] = ids[i];
  }
}

// Each turn, from dl_pad_poll: the controllers that press something take their places.
static void seats_now(void) {
  static int p2_was = -1;
  if (dl_game->players < 2) return;
  if (dl_cfg.p2_pad != p2_was) seat[0] = seat[1] = -1; // the places taken again
  p2_was = dl_cfg.p2_pad;
  int ids[PADS_MAX];
  u32 used[PADS_MAX];
  int n = 0;
  if (pads_n == 0 && joy_present) {
    ids[0] = joy_sticks.id;
    used[0] = joy_raw | joy_dirs;
    n = 1;
  }
  for (int i = 0; i < pads_n; i++, n++) {
    ids[n] = pads_now[i].id;
    used[n] = pads_now[i].raw;
  }
  seats_follow(ids, used, n, dl_cfg.p2_pad != 0);
}

void dl_game_input(DlInput in[2]) {
  memset(in, 0, 2 * sizeof *in);
  u32 offered = ((1u << DL_BTNS_CLASSIC) - 1) | (dl_game->extra_buttons & ~((1u << DL_BTNS_CLASSIC) - 1));
  if (GetForegroundWindow() == dl_hwnd)
    for (int k = 0; k < DL_BTNS; k++)
      if (dl_key_vk[k] && GetAsyncKeyState(dl_key_vk[k]) < 0) in[0].buttons |= 1u << k;
  // the controllers (the XInput slots, the PlayStation pads; a WinMM one alone): one player has them all,
  // two have the one each took (seats_now)
  bool two = dl_game->players >= 2, joy = pads_n == 0 && joy_present;
  int n = joy ? 1 : pads_n;
  for (int i = 0; i < n; i++) {
    const DlPadRead *pr = joy ? &joy_sticks : &pads_now[i];
    int p = !two ? 0 : pr->id == seat[0] ? 0 : pr->id == seat[1] ? 1 : -1;
    if (p < 0) continue;
    in[p].buttons |= joy ? pad_buttons(joy_raw, true, joy_dirs & ~joy_hat) | joy_hat : pad_buttons(pr->raw, false, 0);
    if (dl_game->analog) sticks_take(&in[p], pr);
  }
  in[0].buttons &= offered;
  in[1].buttons &= offered;
  if (wait_release) {
    if (in[0].buttons || in[1].buttons) {
      in[0].buttons = in[1].buttons = 0;
      return;
    }
    wait_release = false;
  }
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

// ---- vibration (dl_rumble): what the game wants, sent when it changes, nothing outside the game
static int rumble_l, rumble_r, sent_l = -1, sent_r = -1;

void dl_rumble(int left, int right) {
  rumble_l = left < 0 ? 0 : left > 65535 ? 65535 : left;
  rumble_r = right < 0 ? 0 : right > 65535 ? 65535 : right;
}

static void rumble_send(int l, int r) {
  sent_l = l;
  sent_r = r;
  XINPUT_VIBRATION v = {(WORD)l, (WORD)r};
  for (DWORD slot = 0; slot < 4 && xinput_set; slot++)
    if (xi_connected[slot]) xinput_set(slot, &v);
  if (sony_present) dl_sony_rumble(l >> 8, r >> 8);
}

static void rumble_follow(void) {
  bool on = dl_cfg.rumble && dl_mode == M_GAME && dl_started && GetForegroundWindow() == dl_hwnd;
  int l = on ? rumble_l : 0, r = on ? rumble_r : 0;
  if (l != sent_l || r != sent_r) rumble_send(l, r);
}

void dl_rumble_stop(void) {
  rumble_l = rumble_r = 0;
  if (sent_l || sent_r) rumble_send(0, 0);
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
  if (dl_input_kind == DL_IN_PS_HID || (sony_present && !xinput_present)) { // a PlayStation pad's own names
    static const wchar_t *const ps[18] = {NULL, NULL, NULL, NULL, L"Options", L"Share", NULL, NULL, L"L1", L"R1", L"PS", NULL, NULL, NULL, NULL, NULL, L"L2", L"R2"};
    if (code >= 12 && code <= 15) return TX(T_PS_CROSS + code - 12);
    if (code == 11) return TX(T_PS_TOUCH);
    if (code >= 0 && code < 18 && ps[code]) return ps[code];
  }
  return code >= 0 && code < 18 ? names[code] : TX(T_NONE);
}

// ---- each turn of the main loop: the controllers read once; in the setup screen they move in it
// (directions with auto-repeat, A or Start chooses, B goes back, Y erases), in the game they reach the
// program's functions; a button being waited for is assigned
void dl_pad_poll(void) {
  static u32 prev;
  static DWORD prev_joy, repeat_at;
  static bool held;
  pads_n = 0;
  pad_raw = xinput_poll();
  int sony_n = dl_sony_poll(pads_now + pads_n, PADS_MAX - pads_n);
  sony_present = sony_n > 0;
  sony_raw = 0;
  for (int k = 0; k < sony_n; k++) sony_raw |= pads_now[pads_n + k].raw;
  pads_n += sony_n;
  pad_raw |= sony_raw;
  if (!xinput_present && !sony_present) joy_poll(); // a PlayStation pad read directly is not read again by WinMM
  else joy_present = false;
  seats_now();
  rumble_follow();
  static u32 prev_sony;
  bool from_sony = (sony_raw & ~prev_sony) != 0;
  prev_sony = sony_raw;
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
  if (pressed || jpressed || (dirs & ~prev_dirs)) dl_input_used(from_sony ? DL_IN_PS_HID : !joy ? DL_IN_XBOX : joy_ps ? DL_IN_PS : DL_IN_PAD); // the footer shows its buttons
  prev_dirs = dirs;
#define FN_PRESSED(k) ((dl_pad_code[k] >= 0 && (pressed & (1u << dl_pad_code[k]))) || (joy && dl_joy_code[k] >= 0 && (jpressed & (1u << dl_joy_code[k]))))
  if (dl_mode == M_GAME) {
    bool menu = FN_PRESSED(FN_MENU);
    if (dl_game->pad_menu == DL_MENU_HOLD) { // only once held 0.6 s; a short press stays the game's
      static DWORD since;
      static bool opened;
      menu = false;
      if (!fn_held(FN_MENU)) since = 0, opened = false;
      else if (!since) since = GetTickCount() | 1;
      else if (!opened && GetTickCount() - since >= 600) opened = menu = true;
    }
    if (menu) dl_pause_open();
    else if (FN_PRESSED(FN_SAVE)) dl_quick_save();
    else if (FN_PRESSED(FN_LOAD)) dl_quick_load();
    else if (FN_PRESSED(FN_SHOT) && !dl_over) dl_screenshot();
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
  if (pressed & (1u << 14)) dl_key(VK_F2);                   // X: a slot's name (the keyboard on screen)
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

static void (*digits_pressed)(int digit);
void dl_digits(void (*pressed)(int digit)) { digits_pressed = pressed; }

// A key pressed while the game is played: the program's shortcuts. True when it was one.
bool dl_game_key(int vk) {
  if (vk == VK_ESCAPE) { dl_pause_open(); return true; }
  if (dl_game->save_state) {
    if (vk == dl_hot_vk[HK_SAVE]) { dl_quick_save(); return true; }
    if (vk == dl_hot_vk[HK_LOAD]) { dl_quick_load(); return true; }
    if (vk == dl_hot_vk[HK_PREV]) { dl_slot_step(-1); return true; }
    if (vk == dl_hot_vk[HK_NEXT]) { dl_slot_step(1); return true; }
  }
  if (vk == dl_hot_vk[HK_SHOT] && !dl_over) { dl_screenshot(); return true; } // over the game: its own picture, window and keys
  if (vk == dl_hot_vk[HK_FULL] && !dl_over) { dl_toggle_fullscreen(); return true; }
  for (int k = 0; k < dl_func_n; k++)
    if (dl_funcs[k].key && vk == dl_funcs[k].key) { dl_funcs[k].run(); return true; }
  // dl_digits: 1 to 9, the top row's (VK '1'..'9') or the keypad's (VK_NUMPAD1..9), when no button has it
  int digit = vk >= '1' && vk <= '9' ? vk - '0' : vk >= 0x61 && vk <= 0x69 ? vk - 0x60 : 0;
  if (digit && digits_pressed) {
    for (int k = 0; k <= DL_BTNS; k++)
      if (dl_key_vk[k] == vk) return false;
    digits_pressed(digit);
    return true;
  }
  return false;
}
