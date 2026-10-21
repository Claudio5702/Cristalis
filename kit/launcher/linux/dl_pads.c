// Controllers on Linux, through SDL3: the shared code (dl_input.c) reads Xbox-type pads by XInput and
// PlayStation ones by dl_sony_poll, as on Windows; here both come from SDL's gamepads. PlayStation pads
// (by SDL's type) go to dl_sony_poll, so that the footer and the Controls pages show their own buttons;
// every other pad is an XInput slot. Their rumble too.
#include "dl_int.h"
#include <SDL3/SDL.h>

#define PADS 8
static SDL_Gamepad *pads[PADS];

static bool is_ps(SDL_Gamepad *g) {
  SDL_GamepadType t = SDL_GetGamepadType(g);
  return t == SDL_GAMEPAD_TYPE_PS3 || t == SDL_GAMEPAD_TYPE_PS4 || t == SDL_GAMEPAD_TYPE_PS5;
}

void dl_pads_added(SDL_JoystickID id) {
  for (int k = 0; k < PADS; k++)
    if (!pads[k]) { pads[k] = SDL_OpenGamepad(id); return; }
}

void dl_pads_removed(SDL_JoystickID id) {
  for (int k = 0; k < PADS; k++)
    if (pads[k] && SDL_GetGamepadID(pads[k]) == id) { SDL_CloseGamepad(pads[k]); pads[k] = NULL; }
}

// The n-th pad of one kind (PlayStation or not).
static SDL_Gamepad *pad_of(bool ps, int n) {
  for (int k = 0; k < PADS; k++)
    if (pads[k] && is_ps(pads[k]) == ps && n-- == 0) return pads[k];
  return NULL;
}

// A pad's state as XInput's.
static void state_of(SDL_Gamepad *g, XINPUT_GAMEPAD *x) {
  static const struct { SDL_GamepadButton b; WORD bit; } map[] = {
      {SDL_GAMEPAD_BUTTON_DPAD_UP, 0x0001}, {SDL_GAMEPAD_BUTTON_DPAD_DOWN, 0x0002}, {SDL_GAMEPAD_BUTTON_DPAD_LEFT, 0x0004}, {SDL_GAMEPAD_BUTTON_DPAD_RIGHT, 0x0008},
      {SDL_GAMEPAD_BUTTON_START, 0x0010}, {SDL_GAMEPAD_BUTTON_BACK, 0x0020}, {SDL_GAMEPAD_BUTTON_LEFT_STICK, 0x0040}, {SDL_GAMEPAD_BUTTON_RIGHT_STICK, 0x0080},
      {SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, 0x0100}, {SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, 0x0200}, {SDL_GAMEPAD_BUTTON_GUIDE, 0x0400}, {SDL_GAMEPAD_BUTTON_TOUCHPAD, 0x0800},
      {SDL_GAMEPAD_BUTTON_SOUTH, 0x1000}, {SDL_GAMEPAD_BUTTON_EAST, 0x2000}, {SDL_GAMEPAD_BUTTON_WEST, 0x4000}, {SDL_GAMEPAD_BUTTON_NORTH, 0x8000}};
  memset(x, 0, sizeof *x);
  for (size_t k = 0; k < sizeof map / sizeof map[0]; k++)
    if (SDL_GetGamepadButton(g, map[k].b)) x->wButtons |= map[k].bit;
  x->bLeftTrigger = (BYTE)(SDL_GetGamepadAxis(g, SDL_GAMEPAD_AXIS_LEFT_TRIGGER) >> 7);
  x->bRightTrigger = (BYTE)(SDL_GetGamepadAxis(g, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) >> 7);
  int ly = -SDL_GetGamepadAxis(g, SDL_GAMEPAD_AXIS_LEFTY), ry = -SDL_GetGamepadAxis(g, SDL_GAMEPAD_AXIS_RIGHTY); // XInput: up is positive
  x->sThumbLX = SDL_GetGamepadAxis(g, SDL_GAMEPAD_AXIS_LEFTX);
  x->sThumbLY = (SHORT)(ly > 32767 ? 32767 : ly);
  x->sThumbRX = SDL_GetGamepadAxis(g, SDL_GAMEPAD_AXIS_RIGHTX);
  x->sThumbRY = (SHORT)(ry > 32767 ? 32767 : ry);
}

DWORD WINAPI dl_xinput_get(DWORD slot, XINPUT_STATE *st) {
  SDL_Gamepad *g = pad_of(false, (int)slot);
  if (!g) return 1167; // ERROR_DEVICE_NOT_CONNECTED
  st->dwPacketNumber++;
  state_of(g, &st->Gamepad);
  return ERROR_SUCCESS;
}

DWORD WINAPI dl_xinput_set(DWORD slot, XINPUT_VIBRATION *v) {
  SDL_Gamepad *g = pad_of(false, (int)slot);
  if (!g) return 1167;
  SDL_RumbleGamepad(g, v->wLeftMotorSpeed, v->wRightMotorSpeed, v->wLeftMotorSpeed || v->wRightMotorSpeed ? 10000 : 0);
  return ERROR_SUCCESS;
}

// ---- PlayStation pads: each one's codes as dl_sony.c gives them on Windows (XInput's, and the left
// stick), and its sticks
int dl_sony_poll(DlPadRead *out, int max) {
  int got = 0;
  for (int n = 0; got < max; n++) {
    SDL_Gamepad *g = pad_of(true, n);
    if (!g) break;
    XINPUT_GAMEPAD x;
    state_of(g, &x);
    u32 r = x.wButtons;
    if (x.bLeftTrigger > 100) r |= 1u << 16;
    if (x.bRightTrigger > 100) r |= 1u << 17;
    if (x.sThumbLY > 16000) r |= 1u << 18;
    if (x.sThumbLY < -16000) r |= 1u << 19;
    if (x.sThumbLX < -16000) r |= 1u << 20;
    if (x.sThumbLX > 16000) r |= 1u << 21;
    out[got++] = (DlPadRead){r, x.sThumbLX, x.sThumbLY, x.sThumbRX, x.sThumbRY};
  }
  return got;
}

void dl_sony_rumble(int left, int right) {
  for (int n = 0;; n++) {
    SDL_Gamepad *g = pad_of(true, n);
    if (!g) break;
    SDL_RumbleGamepad(g, (Uint16)(left * 257), (Uint16)(right * 257), left || right ? 10000 : 0);
  }
}

void dl_sony_rescan(void) {} // SDL says when pads come and go (dl_win32.c)
