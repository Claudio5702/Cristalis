// The program around the game: settings file, window and fullscreen, the main loop (one game frame
// at the game's rate), sound output, keyboard and controller, the picture with its filters, menu
// sounds and the command line (--ui-audit DIR, --gpu-check DIR, --nomouse, --ini FILE).
#include "dl_int.h"
#include <math.h>
#include <mmsystem.h>
#include <dwmapi.h>

DlCfg dl_cfg = {.ui_lang = 2, .volume = 10, .menu_sound = 1, .gpu = 1, .turbo = 0, .pause_menu = 1, .bg_pause = 1, .rumble = 1};
HWND dl_hwnd;
char dl_base_dir[MAX_PATH];
const DlGame *dl_game;
u32 *dl_pic;
int dl_pic_w, dl_pic_h, dl_pic_scale = 1;
double dl_pic_aspect;
bool dl_started, dl_running = true, dl_no_mouse;
const char *dl_audit_dir;
static const char *gpu_check_dir;
static int bench_secs; // --bench N: N seconds of play, then the frame rate and the sound's gaps
bool dl_start_done;
static bool play_now;  // --play: straight into the game, without the setup screen
static bool relaunch;  // dl_relaunch: started again once closed

void dl_relaunch(void) {
  relaunch = true;
  dl_running = false;
}

// ---------------------------------------------------------------- settings file
// Each value: its name in the file, its limits, its default, and what "Defaults" does with it
// (0: kept, as the language and the window's place; 1: a picture setting; 2: any other).
typedef struct { const char *name; int *value; int lo, hi, def, group; } Setting;
static Setting settings[160];
static int setting_n;
static char ini_path[MAX_PATH];

static void add_setting(const char *name, int *value, int lo, int hi, int group) {
  if (setting_n >= (int)(sizeof settings / sizeof settings[0])) return;
  settings[setting_n++] = (Setting){name, value, lo, hi, *value, group};
}

void dl_setting(const char *name, int *value, int lo, int hi, bool keep) { add_setting(name, value, lo, hi, keep ? 0 : 2); }

static void settings_load(void) {
  FILE *f = fopen(ini_path, "r");
  if (!f) return;
  char line[160], name[96];
  int v;
  while (fgets(line, sizeof line, f)) {
    if (sscanf(line, " %95[^= ] = %d", name, &v) != 2) continue;
    for (int k = 0; k < setting_n; k++)
      if (!strcmp(name, settings[k].name) && v >= settings[k].lo && v <= settings[k].hi) *settings[k].value = v;
  }
  fclose(f);
}

static int saved[160]; // the values as last written

// Safe mode: the values the player had when the run started, and the ones safe mode put instead.
// A value still the forced one is written as the player's.
bool dl_safe;
static int safe_orig[160], safe_forced[160];

static void settings_save(void) {
  FILE *f = fopen(ini_path, "w");
  if (!f) return;
  for (int k = 0; k < setting_n; k++) {
    int v = *settings[k].value;
    fprintf(f, "%s=%d\n", settings[k].name, dl_safe && v == safe_forced[k] ? safe_orig[k] : v);
    saved[k] = v;
  }
  fclose(f);
}

// Once a second while the program runs: a setting that changed is written at once, so that nothing
// is lost if the program does not close normally.
static void settings_follow(void) {
  static DWORD at;
  DWORD t = GetTickCount();
  if (t - at < 1000) return;
  at = t;
  for (int k = 0; k < setting_n; k++)
    if (saved[k] != *settings[k].value) { settings_save(); return; }
}

// ---------------------------------------------------------------- safe mode
// While the program runs, "<settings file>.running" holds its process id; it is removed when the
// program closes normally. Found at start while no such program runs, it says the last run ended
// otherwise (a crash, a driver, the power...): this run starts in safe mode.
static char run_mark[MAX_PATH];

static bool same_program_runs(DWORD pid) {
  HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
  if (!h) return false;
  wchar_t a[MAX_PATH], b[MAX_PATH];
  DWORD n = MAX_PATH, code = 0;
  bool same = GetExitCodeProcess(h, &code) && code == STILL_ACTIVE && QueryFullProcessImageNameW(h, 0, a, &n) &&
              GetModuleFileNameW(NULL, b, MAX_PATH) && !lstrcmpiW(a, b);
  CloseHandle(h);
  return same;
}

static bool last_run_failed(void) {
  snprintf(run_mark, sizeof run_mark, "%s.running", ini_path);
  FILE *f = fopen(run_mark, "r");
  if (!f) return false;
  unsigned long pid = 0;
  int got = fscanf(f, "%lu", &pid);
  fclose(f);
  return !(got == 1 && pid != GetCurrentProcessId() && same_program_runs((DWORD)pid)); // another copy running: no crash
}

static void run_mark_write(void) {
  FILE *f = fopen(run_mark, "w");
  if (f) { fprintf(f, "%lu\n", (unsigned long)GetCurrentProcessId()); fclose(f); }
}

static void run_mark_clear(void) { if (run_mark[0]) DeleteFileA(run_mark); }

static void safe_start(void) {
  for (int k = 0; k < setting_n; k++) safe_orig[k] = *settings[k].value;
  dl_cfg.gpu = 0;
  dl_cfg.hdr = 0;
  dl_cfg.crt = 0;
  dl_cfg.vsync = 0;
  dl_cfg.fullscreen = 0;
  if (dl_game->safe_mode) dl_game->safe_mode();
  for (int k = 0; k < setting_n; k++) safe_forced[k] = *settings[k].value;
  dl_safe = true;
}

void dl_settings_sibling(char *out, const char *ext) {
  snprintf(out, MAX_PATH, "%s", ini_path);
  char *dot = strrchr(out, '.'), *slash = strrchr(out, DL_SEPC);
  if (dot && (!slash || dot > slash)) *dot = 0;
  size_t n = strlen(out);
  snprintf(out + n, MAX_PATH - n, "%s", ext);
}

void dl_safe_restart(void) {
  for (int k = 0; k < setting_n; k++) *settings[k].value = safe_orig[k];
  dl_safe = false;
  dl_relaunch();
}

void dl_defaults(bool picture_only) {
  int gpu = dl_cfg.gpu;
  for (int k = 0; k < setting_n; k++)
    if (settings[k].group == 1 || (!picture_only && settings[k].group == 2)) *settings[k].value = settings[k].def;
  if (dl_cfg.gpu != gpu) dl_gpu_restart();
  dl_gpu_follow(true);
  dl_dirty = true;
}

// The languages of the game's texts (DlGame.langs; 0: the six before Italian).
bool dl_lang_ok(int lang) {
  unsigned langs = dl_game ? dl_game->langs & DL_LANGS_ALL : 0;
  if (!langs) langs = (1u << DL_LANG_IT) - 1;
  return lang >= 0 && lang < DL_LANGS && (langs >> lang & 1);
}

// The language of Windows, until the settings file says otherwise.
int dl_lang_default(void) {
  int lang;
  switch (GetUserDefaultUILanguage() & 0x3FF) {
    case 0x11: lang = DL_LANG_JP; break;
    case 0x0C: lang = DL_LANG_FR; break;
    case 0x0A: lang = DL_LANG_ES; break;
    case 0x16: lang = DL_LANG_BR; break;
    case 0x07: lang = DL_LANG_DE; break;
    case 0x10: lang = DL_LANG_IT; break;
    default: lang = DL_LANG_EN; break;
  }
  if (!dl_lang_ok(lang)) lang = DL_LANG_EN;
  for (int k = 0; !dl_lang_ok(lang) && k < DL_LANGS; k++) lang = k; // a game without English
  return lang;
}

// ---------------------------------------------------------------- picture
static u32 *scan_buf, *cvd_buf;

// The colour-blind filter (dl_cfg.cvd; the same as cvd_fix in dl_gpu.c): what the eye sees is worked
// out (Machado's matrices), and what it misses is moved into the colours it does see.
static void cvd_apply(const u32 *src, u32 *dst, size_t n, int kind) {
  static const double sim[3][9] = {
      {0.152286, 1.052583, -0.204868, 0.114503, 0.786281, 0.099216, -0.003882, -0.048116, 1.051998},
      {0.367322, 0.860646, -0.227968, 0.280085, 0.672501, 0.047413, -0.011820, 0.042940, 0.968881},
      {1.255528, -0.076749, -0.178779, -0.078411, 0.930809, 0.147602, 0.004733, 0.691367, 0.303900}};
  const double *m = sim[kind - 1];
  for (size_t i = 0; i < n; i++) {
    u32 p = src[i];
    double c[3] = {(p >> 16 & 255) / 255.0, (p >> 8 & 255) / 255.0, (p & 255) / 255.0}, e[3], o[3];
    for (int k = 0; k < 3; k++) e[k] = c[k] - (m[k * 3] * c[0] + m[k * 3 + 1] * c[1] + m[k * 3 + 2] * c[2]);
    if (kind < 3) { o[0] = c[0]; o[1] = c[1] + 0.7 * e[0] + e[1]; o[2] = c[2] + 0.7 * e[0] + e[2]; }
    else { o[0] = c[0] + e[0] + 0.7 * e[2]; o[1] = c[1] + e[1] + 0.7 * e[2]; o[2] = c[2]; }
    u32 q = 0;
    for (int k = 0; k < 3; k++) { int v = (int)(o[k] * 255 + 0.5); q = q << 8 | (u32)(v < 0 ? 0 : v > 255 ? 255 : v); }
    dst[i] = q;
  }
}
int dl_fps_shown;

void dl_fit(int cw, int ch, bool integer, int *w, int *h) {
  int gh = dl_pic_h;
  double a = dl_pic_aspect;
  int sw = cw, sh = (int)(cw / a);
  if (sh > ch) { sh = ch; sw = (int)(ch * a); }
  if (integer && sh >= gh) {
    sh = sh / gh * gh;
    sw = (int)(sh * a);
  }
  *w = sw;
  *h = sh;
}

// The game's picture into a rectangle, with the filter and the scanlines; the frame rate on top.
void dl_blit(HDC dc, int x, int y, int w, int h) {
  if (w <= 0 || h <= 0 || !dl_pic) return;
  int gw = dl_pic_w, gh = dl_pic_h, sh = gh;
  const u32 *src = dl_pic;
  if (dl_cfg.scanlines) {
    // each line three times, the third one darker
    u32 keep = (u32)(256 - dl_cfg.scanlines * 48);
    for (int yy = 0; yy < gh; yy++) {
      const u32 *row = &dl_pic[(size_t)yy * gw];
      u32 *o = &scan_buf[(size_t)yy * 3 * gw];
      memcpy(o, row, (size_t)gw * 4);
      memcpy(o + gw, row, (size_t)gw * 4);
      for (int xx = 0; xx < gw; xx++) {
        u32 c = row[xx];
        o[2 * gw + xx] = ((((c >> 16) & 255) * keep >> 8) << 16) | ((((c >> 8) & 255) * keep >> 8) << 8) | ((c & 255) * keep >> 8);
      }
    }
    src = scan_buf;
    sh = gh * 3;
  }
  if (dl_cfg.cvd > 0 && dl_cfg.cvd < 4 && cvd_buf) {
    cvd_apply(src, cvd_buf, (size_t)gw * sh, dl_cfg.cvd);
    src = cvd_buf;
  }
  BITMAPINFO bi = {0};
  bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  bi.bmiHeader.biWidth = gw;
  bi.bmiHeader.biHeight = -sh;
  bi.bmiHeader.biPlanes = 1;
  bi.bmiHeader.biBitCount = 32;
  if (dl_cfg.smooth) { SetStretchBltMode(dc, HALFTONE); SetBrushOrgEx(dc, 0, 0, NULL); }
  else SetStretchBltMode(dc, COLORONCOLOR);
  StretchDIBits(dc, x, y, w, h, 0, 0, gw, sh, src, &bi, DIB_RGB_COLORS, SRCCOPY);
  if (dl_cfg.show_fps) dl_draw_fps(dc, x, y, h);
}

void dl_draw_fps(HDC dc, int x, int y, int h) {
  static HFONT f;
  static int fh;
  int want = h / 24 < 10 ? 10 : h / 24;
  if (want != fh) { if (f) DeleteObject(f); fh = want; f = CreateFontW(-fh, 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, ANTIALIASED_QUALITY, 0, L"Verdana"); }
  wchar_t t[16];
  wsprintfW(t, L"%d FPS", dl_fps_shown);
  HGDIOBJ of = SelectObject(dc, f);
  SetBkMode(dc, TRANSPARENT);
  SetTextAlign(dc, TA_LEFT | TA_TOP);
  SetTextColor(dc, RGB(0, 0, 0));
  TextOutW(dc, x + fh / 2 + 1, y + fh / 3 + 1, t, lstrlenW(t));
  SetTextColor(dc, RGB(255, 255, 120));
  TextOutW(dc, x + fh / 2, y + fh / 3, t, lstrlenW(t));
  SelectObject(dc, of);
}

void dl_picture_size(int w, int h, double aspect) {
  if (w < 1 || h < 1 || w > dl_game->width || h > dl_game->height) return;
  if (aspect <= 0) aspect = (double)w / h;
  if (w != dl_pic_w || h != dl_pic_h || aspect != dl_pic_aspect) dl_dirty = true; // the preview's place changes
  dl_pic_w = w;
  dl_pic_h = h;
  dl_pic_aspect = aspect;
}

// The game's pictures over its own (DlGame.overlays), colours multiplied by their opacity.
void dl_draw_overlays(HDC dc, int x, int y, int w, int h) {
  DlOverlay ov[DL_MAX_OVERLAYS];
  int n = dl_game->overlays ? dl_game->overlays(x, y, w, h, ov, DL_MAX_OVERLAYS) : 0;
  for (int k = 0; k < n && k < DL_MAX_OVERLAYS; k++) {
    if (!ov[k].pixels || ov[k].w <= 0 || ov[k].h <= 0) continue;
    BITMAPINFO bi = {{sizeof(BITMAPINFOHEADER), ov[k].w, -ov[k].h, 1, 32}};
    void *bits = NULL;
    HDC src = CreateCompatibleDC(dc);
    HBITMAP bmp = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    if (bmp && bits) {
      memcpy(bits, ov[k].pixels, (size_t)ov[k].w * ov[k].h * 4);
      HGDIOBJ old = SelectObject(src, bmp);
      BLENDFUNCTION bf = {AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
      AlphaBlend(dc, ov[k].x, ov[k].y, ov[k].w, ov[k].h, src, 0, 0, ov[k].w, ov[k].h, bf);
      SelectObject(src, old);
    }
    if (bmp) DeleteObject(bmp);
    DeleteDC(src);
  }
}

int dl_screen_hz(void) {
  static DWORD at;
  static int hz;
  DWORD t = GetTickCount();
  if (hz && t - at < 2000) return hz;
  at = t;
  hz = 60;
  MONITORINFOEXW mi = {.cbSize = sizeof mi};
  DEVMODEW dm = {.dmSize = sizeof dm};
  if (GetMonitorInfoW(MonitorFromWindow(dl_hwnd, MONITOR_DEFAULTTOPRIMARY), (MONITORINFO *)&mi) &&
      EnumDisplaySettingsW(mi.szDevice, ENUM_CURRENT_SETTINGS, &dm) && dm.dmDisplayFrequency > 1)
    hz = (int)dm.dmDisplayFrequency;
  return hz;
}

void dl_picture_scale(int s) { dl_pic_scale = s < 1 ? 1 : s; }

// The game alone in the window, black around it.
static void present(HDC dc) {
  RECT rc;
  GetClientRect(dl_hwnd, &rc);
  int cw = rc.right, ch = rc.bottom, w, h;
  dl_fit(cw, ch, dl_cfg.integer_scale, &w, &h);
  int ox = (cw - w) / 2, oy = (ch - h) / 2;
  dl_blit(dc, ox, oy, w, h);
  dl_draw_overlays(dc, ox, oy, w, h);
  int fh = h / 22 < 14 ? 14 : h / 22;
  dl_draw_note(dc, ox + fh, oy + h - fh * 5 / 2, h);
  if (dl_toast_now()) { // an achievement just unlocked, at the top right
    int tw, tb, m = h / 30;
    dl_toast_size(dc, h, &tw, &tb);
    dl_draw_toast(dc, ox + w - tw - m, oy + m, h);
  }
  HBRUSH black = GetStockObject(BLACK_BRUSH);
  if (ox > 0) { RECT l = {0, 0, ox, ch}, r = {ox + w, 0, cw, ch}; FillRect(dc, &l, black); FillRect(dc, &r, black); }
  if (oy > 0) { RECT t = {0, 0, cw, oy}, b = {0, oy + h, cw, ch}; FillRect(dc, &t, black); FillRect(dc, &b, black); }
}

// ---------------------------------------------------------------- window
static WINDOWPLACEMENT win_prev = {sizeof win_prev};
static bool is_fullscreen;

void dl_toggle_fullscreen(void) {
  DWORD style = (DWORD)GetWindowLongW(dl_hwnd, GWL_STYLE);
  if (style & WS_OVERLAPPEDWINDOW) {
    MONITORINFO mi = {sizeof mi};
    if (GetWindowPlacement(dl_hwnd, &win_prev) && GetMonitorInfoW(MonitorFromWindow(dl_hwnd, MONITOR_DEFAULTTOPRIMARY), &mi)) {
      SetWindowLongW(dl_hwnd, GWL_STYLE, (LONG)(style & ~(DWORD)WS_OVERLAPPEDWINDOW));
      SetWindowPos(dl_hwnd, HWND_TOP, mi.rcMonitor.left, mi.rcMonitor.top, mi.rcMonitor.right - mi.rcMonitor.left,
                   mi.rcMonitor.bottom - mi.rcMonitor.top, SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
      is_fullscreen = true;
    }
  } else {
    SetWindowLongW(dl_hwnd, GWL_STYLE, (LONG)(style | WS_OVERLAPPEDWINDOW));
    SetWindowPlacement(dl_hwnd, &win_prev);
    SetWindowPos(dl_hwnd, NULL, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
    is_fullscreen = false;
  }
  dl_cfg.fullscreen = is_fullscreen;
  dl_dirty = true;
}

bool dl_in_game(void) { return dl_mode == M_GAME && dl_started; }

void dl_resume(void) {
  if (dl_started) dl_play();
}

void dl_stop(void) {
  dl_started = false;
  dl_state_taken = false;
  if (dl_mode == M_GAME || dl_mode == M_PAUSE || dl_mode == M_TRY) dl_menu_open();
  dl_dirty = true;
}
bool dl_gpu_on(void) { return dl_gpu_ready(); }

void dl_play(void) {
  // begin: a first start from the game as it runs behind the setup screen, not from a state just
  // loaded (the session, a slot)
  if (!dl_started && !dl_state_taken && dl_game->begin) dl_game->begin();
  dl_state_taken = false;
  dl_started = true;
  dl_mode = M_GAME;
  dl_input_release(); // the key or button that chose this does not reach the game
  InvalidateRect(dl_hwnd, NULL, FALSE);
}

void dl_menu_open(void) {
  dl_mode = M_SIDE;
  dl_side = 0;
  dl_menu_reread();
  dl_dirty = true;
}

void dl_restart(void) {
  if (dl_game->reset) dl_game->reset();
  if (dl_started) dl_play();
}

// Short sounds of the setup screen (made once per volume): a tick when moving, two notes when choosing,
// in the theme's kind (DL_BEEP_*).
void dl_beep(int kind) {
  static u8 wav[2][44 + 2 * 6000];
  static int made_for = -1;
  if (!dl_cfg.menu_sound || !dl_cfg.volume || dl_audit_dir) return;
  const void *own = ui_theme_sound(kind, dl_cfg.volume); // the game's own, if its theme has them
  if (own) { PlaySoundA(own, NULL, SND_MEMORY | SND_ASYNC | SND_NODEFAULT); return; }
  if (made_for != dl_cfg.volume) {
    made_for = dl_cfg.volume;
    int style = dl_game->theme ? dl_game->theme->beep : DL_BEEP_BELL;
    for (int w = 0; w < 2; w++) {
      const int rate = 22050;
      int n = style == DL_BEEP_CHIP ? (w ? 2400 : 900) : style == DL_BEEP_SOFT ? (w ? 1800 : 700) : style == DL_BEEP_CRYSTAL ? (w ? 6000 : 2600) : (w ? 1400 : 700);
      u32 hd[11] = {0x46464952, 36 + (u32)n * 2, 0x45564157, 0x20746D66, 16, 0x00010001, (u32)rate, (u32)rate * 2, 0x00100002, 0x61746164, (u32)n * 2};
      memcpy(wav[w], hd, 44);
      for (int k = 0; k < n; k++) {
        double env = 1.0 - (double)k / n, s;
        if (style == DL_BEEP_CHIP) { // square wave
          double f = w ? (k < n / 2 ? 784.0 : 1175.0) : 1568.0;
          s = (fmod(k * f / rate, 1.0) < 0.5 ? 0.6 : -0.6) * env;
        } else if (style == DL_BEEP_SOFT) { // low triangle, quickly gone
          double f = w ? (k < n / 2 ? 523.0 : 659.0) : 440.0, ph = fmod(k * f / rate, 1.0);
          s = (ph < 0.5 ? 4 * ph - 1 : 3 - 4 * ph) * env * env * env * 1.4;
        } else if (style == DL_BEEP_CRYSTAL) { // high chime with overtones, a long ring
          double f = w ? (k < n / 3 ? 1760.0 : 2637.0) : 2093.0, t = k * 6.283185307 * f / rate;
          s = (sin(t) + 0.3 * sin(2 * t) + 0.15 * sin(3.01 * t)) * exp(-5.0 * k / n) * 0.8;
        } else {
          double f = w ? (k < n / 2 ? 990.0 : 1320.0) : 880.0;
          s = sin(k * 6.283185307 * f / rate) * env * env;
        }
        s16 v = (s16)(s * 420 * dl_cfg.volume);
        memcpy(wav[w] + 44 + k * 2, &v, 2);
      }
    }
  }
  PlaySoundA((LPCSTR)wav[kind], NULL, SND_MEMORY | SND_ASYNC | SND_NODEFAULT);
}

// ---------------------------------------------------------------- sound: the game's samples go into a
// ring, from which waveOut buffers are filled
#define ABUF_COUNT 6
#define ABUF_FRAMES 512
#define RING 32768 // sample frames
static HWAVEOUT wave_out;
static HANDLE audio_event; // set by the sound device whenever a buffer has been played (NULL: none, Linux)
static WAVEHDR wave_hdr[ABUF_COUNT];
static s16 wave_data[ABUF_COUNT][ABUF_FRAMES * 2];
static s16 ring[RING * 2];
static int audio_stretch; // sample frames added to each picture's (the screen sets the pace and the sound runs short)
static int ring_rd, ring_n;

static void audio_init(void) {
  if (dl_game->audio_rate <= 0) return;
  WAVEFORMATEX fmt = {0};
  fmt.wFormatTag = WAVE_FORMAT_PCM;
  fmt.nChannels = 2;
  fmt.nSamplesPerSec = (DWORD)dl_game->audio_rate;
  fmt.wBitsPerSample = 16;
  fmt.nBlockAlign = 4;
  fmt.nAvgBytesPerSec = fmt.nSamplesPerSec * 4;
  audio_event = CreateEventW(NULL, FALSE, FALSE, NULL);
  if (waveOutOpen(&wave_out, WAVE_MAPPER, &fmt, (DWORD_PTR)audio_event, 0, audio_event ? CALLBACK_EVENT : CALLBACK_NULL) != MMSYSERR_NOERROR) { wave_out = NULL; return; }
  for (int k = 0; k < ABUF_COUNT; k++) {
    wave_hdr[k].lpData = (LPSTR)wave_data[k];
    wave_hdr[k].dwBufferLength = ABUF_FRAMES * 4;
    waveOutPrepareHeader(wave_out, &wave_hdr[k], sizeof(WAVEHDR));
    wave_hdr[k].dwFlags |= WHDR_DONE;
  }
}

static void audio_add(const s16 *s, int n) {
  for (int k = 0; k < n + audio_stretch; k++) {
    int j = k < n ? k : n - 1; // stretched: the last sample frame again
    if (ring_n == RING) { ring_rd = (ring_rd + 1) % RING; ring_n--; } // too far ahead: the oldest goes
    int at = (ring_rd + ring_n) % RING;
    ring[at * 2] = s[j * 2];
    ring[at * 2 + 1] = s[j * 2 + 1];
    ring_n++;
  }
}

// Buffers handed to the sound device and not played yet.
static int audio_queued(void) {
  int n = 0;
  for (int k = 0; k < ABUF_COUNT; k++)
    if (!(wave_hdr[k].dwFlags & WHDR_DONE)) n++;
  return n;
}

static void audio_pump(void) {
  if (!wave_out) return;
  for (int k = 0; k < ABUF_COUNT && ring_n >= ABUF_FRAMES; k++) {
    if (!(wave_hdr[k].dwFlags & WHDR_DONE)) continue;
    for (int i = 0; i < ABUF_FRAMES; i++) {
      wave_data[k][i * 2] = (s16)(ring[ring_rd * 2] * dl_cfg.volume / 10);
      wave_data[k][i * 2 + 1] = (s16)(ring[ring_rd * 2 + 1] * dl_cfg.volume / 10);
      ring_rd = (ring_rd + 1) % RING;
    }
    ring_n -= ABUF_FRAMES;
    wave_hdr[k].dwFlags &= ~(DWORD)WHDR_DONE;
    waveOutWrite(wave_out, &wave_hdr[k], sizeof(WAVEHDR));
  }
}

// ---------------------------------------------------------------- PNG (stored, no compression) for --ui-audit
static u32 crc_table[256];
static u32 crc_update(u32 c, const u8 *p, size_t n) {
  if (!crc_table[1])
    for (u32 i = 0; i < 256; i++) {
      u32 v = i;
      for (int k = 0; k < 8; k++) v = v & 1 ? 0xEDB88320u ^ (v >> 1) : v >> 1;
      crc_table[i] = v;
    }
  for (size_t i = 0; i < n; i++) c = crc_table[(c ^ p[i]) & 255] ^ (c >> 8);
  return c;
}
static void put32(u8 *p, u32 v) { p[0] = (u8)(v >> 24); p[1] = (u8)(v >> 16); p[2] = (u8)(v >> 8); p[3] = (u8)v; }
static void png_chunk(FILE *f, const char *type, const u8 *data, u32 len) {
  u8 hdr[8];
  put32(hdr, len);
  memcpy(hdr + 4, type, 4);
  fwrite(hdr, 1, 8, f);
  if (len) fwrite(data, 1, len, f);
  u32 c = crc_update(0xFFFFFFFFu, hdr + 4, 4);
  c = crc_update(c, data, len) ^ 0xFFFFFFFFu;
  u8 crc[4];
  put32(crc, c);
  fwrite(crc, 1, 4, f);
}
bool dl_save_png(const char *path, const u32 *px, int w, int h) {
  FILE *f = fopen(path, "wb");
  if (!f) return false;
  size_t raw_len = (size_t)(w * 3 + 1) * (size_t)h;
  u8 *raw = malloc(raw_len), *z = malloc(raw_len + raw_len / 65535 * 5 + 16);
  if (!raw || !z) { free(raw); free(z); fclose(f); return false; }
  for (int y = 0; y < h; y++) {
    u8 *row = raw + (size_t)y * (size_t)(w * 3 + 1);
    *row++ = 0;
    for (int x = 0; x < w; x++) {
      u32 c = px[(size_t)y * w + x];
      *row++ = (u8)(c >> 16); *row++ = (u8)(c >> 8); *row++ = (u8)c;
    }
  }
  size_t zn = 0;
  z[zn++] = 0x78; z[zn++] = 0x01;
  u32 a1 = 1, a2 = 0;
  for (size_t off = 0; off < raw_len;) {
    size_t n = raw_len - off > 65535 ? 65535 : raw_len - off;
    z[zn++] = off + n == raw_len;
    z[zn++] = (u8)n; z[zn++] = (u8)(n >> 8); z[zn++] = (u8)~n; z[zn++] = (u8)(~n >> 8);
    memcpy(z + zn, raw + off, n);
    zn += n;
    for (size_t i = 0; i < n; i++) { a1 = (a1 + raw[off + i]) % 65521; a2 = (a2 + a1) % 65521; }
    off += n;
  }
  put32(z + zn, (a2 << 16) | a1);
  zn += 4;
  static const u8 sig[8] = {0x89, 'P', 'N', 'G', 13, 10, 26, 10};
  fwrite(sig, 1, 8, f);
  u8 ihdr[13];
  put32(ihdr, (u32)w); put32(ihdr + 4, (u32)h);
  ihdr[8] = 8; ihdr[9] = 2; ihdr[10] = 0; ihdr[11] = 0; ihdr[12] = 0;
  png_chunk(f, "IHDR", ihdr, 13);
  png_chunk(f, "IDAT", z, (u32)zn);
  png_chunk(f, "IEND", (const u8 *)"", 0);
  fclose(f);
  free(raw);
  free(z);
  return true;
}

// ---------------------------------------------------------------- window procedure
static LRESULT CALLBACK wnd_proc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
  if (dl_no_mouse && (msg == WM_MOUSEMOVE || msg == WM_LBUTTONDOWN || msg == WM_RBUTTONDOWN || msg == WM_MOUSEWHEEL)) return 0;
  switch (msg) {
    case WM_MOUSEMOVE:
      if (dl_mode != M_GAME) dl_mouse((short)LOWORD(lp), (short)HIWORD(lp), false);
      return 0;
    case WM_LBUTTONDOWN:
      dl_input_used(DL_IN_KEYS);
      if (dl_mode != M_GAME) dl_mouse((short)LOWORD(lp), (short)HIWORD(lp), true);
      return 0;
    case WM_RBUTTONDOWN:
      if (dl_mode != M_GAME) dl_key(VK_ESCAPE);
      return 0;
    case WM_MOUSEWHEEL:
      if (dl_mode != M_GAME) dl_key((short)HIWORD(wp) > 0 ? VK_UP : VK_DOWN);
      return 0;
    case WM_KEYDOWN:
      dl_input_used(DL_IN_KEYS);
      if (dl_mode == M_GAME) {
        if (!(lp & (1 << 30))) dl_game_key((int)wp);
        return 0;
      }
      if (!dl_wait && (int)wp == dl_hot_vk[HK_FULL]) { dl_toggle_fullscreen(); return 0; }
      if (!(lp & (1 << 30)) || wp == VK_UP || wp == VK_DOWN || wp == VK_LEFT || wp == VK_RIGHT) dl_key((int)wp);
      return 0;
    case WM_CHAR:
      if (dl_mode == M_PAGE && dl_cur == PG_SAVES) dl_saves_char((wchar_t)wp); // a slot's name being typed
      return 0;
    case WM_SYSKEYDOWN:
      if (wp == VK_RETURN) { dl_toggle_fullscreen(); return 0; }
      break;
    case WM_CLOSE: dl_running = false; return 0;
    case WM_DESTROY: PostQuitMessage(0); return 0;
    case WM_SIZE: dl_dirty = true; break;
    case WM_DEVICECHANGE: dl_sony_rescan(); break; // a controller may have come
    case WM_PAINT: {
      PAINTSTRUCT ps;
      HDC dc = BeginPaint(h, &ps);
      if (dl_gpu_ready()) dl_dirty = true; // drawn by the main loop
      else if (dl_mode == M_GAME) present(dc);
      else dl_draw_menu(dc);
      EndPaint(h, &ps);
      return 0;
    }
    case WM_ERASEBKGND: return 1;
  }
  return DefWindowProcW(h, msg, wp, lp);
}

// ---------------------------------------------------------------- using the computer lightly
#ifndef CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
#define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 0x00000002
#endif

// The waits of the main loop: a high resolution timer (Windows 10 1803 and later), so that the
// program sleeps until the next picture instead of waking every millisecond; without one, the
// system's clock at 1 ms (timeBeginPeriod). A message (a key, the mouse, the window) ends a wait.
static HANDLE wait_timer;

static void wait_start(void) {
  wait_timer = CreateWaitableTimerExW(NULL, NULL, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
  if (!wait_timer) timeBeginPeriod(1);
}
static void wait_stop(void) {
  if (wait_timer) CloseHandle(wait_timer);
  else timeEndPeriod(1);
  wait_timer = NULL;
}

// Waits until about ms milliseconds from now, or a message. To the moment (the game's pictures), the
// last quarter of a millisecond is left to the loop, which checks the clock itself: a timer may wake
// a little late. Otherwise (the menus) the whole time is slept.
static void wait_ms(double ms, bool to_the_moment) {
  if (wait_timer) {
    double margin = to_the_moment ? 0.25 : 0;
    if (ms <= margin + 0.05) return;
    LARGE_INTEGER due = {.QuadPart = -(LONGLONG)((ms - margin) * 10000)}; // in 100 ns, from now
    if (SetWaitableTimer(wait_timer, &due, 0, NULL, NULL, FALSE)) {
      MsgWaitForMultipleObjects(1, &wait_timer, FALSE, INFINITE, QS_ALLINPUT);
      return;
    }
  }
  if (ms > 1.5) MsgWaitForMultipleObjects(0, NULL, FALSE, (DWORD)(ms - 1), QS_ALLINPUT);
}

// ---------------------------------------------------------------- --gpu-check DIR
// The graphics card path drawn into its back buffer and read back into PNG files (nothing is taken
// from the screen): the setup screen, the Picture page, the game with each filter, "Try it full
// size". Returns 0 when every picture was made, 3 when the graphics card could not be used.
static int gpu_check(void) {
  static s16 tmp[8192];
  SetWindowPos(dl_hwnd, NULL, 0, 0, 1280 + 16, 800 + 39, SWP_NOMOVE | SWP_NOZORDER);
  ShowWindow(dl_hwnd, SW_SHOWNOACTIVATE);
  dl_cfg.gpu = 1;
  dl_gpu_prepare();
  DWORD until = GetTickCount() + 15000;
  while (!dl_gpu_ready() && GetTickCount() < until) {
    MSG msg;
    while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) DispatchMessageW(&msg);
    Sleep(20);
  }
  if (!dl_gpu_ready()) { printf("gpu check: the graphics card cannot be used\n"); return 3; }
  for (int k = 0; k < 90; k++) dl_game->frame(&(DlInput){k > 30 ? DL_LEFT | DL_UP : 0}, dl_pic, tmp, 4096);
  dl_toast_sample(); // over the game's pictures, when it has achievements
  static const struct { const char *name; int mode, page, smooth, scan, crt, fps; } shots[] = {
      {"menu", M_SIDE, -1, 0, 0, 0, 0},     {"image_page", M_PAGE, DL_PAGE_IMAGE, 0, 0, 0, 0}, {"game_sharp", M_GAME, -1, 0, 0, 0, 1},
      {"game_smooth", M_GAME, -1, 1, 0, 0, 0}, {"game_edges", M_GAME, -1, 2, 0, 0, 0},       {"game_scanlines", M_GAME, -1, 0, 3, 0, 0},
      {"game_crt_flat", M_GAME, -1, 0, 0, 1, 0}, {"game_crt_curved", M_GAME, -1, 0, 0, 2, 0}, {"try", M_TRY, -1, 0, 0, 0, 1},
      {"pause", M_PAUSE, -1, 0, 0, 0, 0},
  };
  int failed = 0;
  for (size_t k = 0; k < sizeof shots / sizeof shots[0]; k++) {
    dl_cfg.pause_menu = 1;
    if (shots[k].mode == M_PAUSE) dl_pause_open();
    dl_mode = shots[k].mode;
    dl_side = 0;
    if (shots[k].page >= 0) { dl_cur = shots[k].page; dl_sel = 2; }
    dl_cfg.smooth = shots[k].smooth;
    dl_cfg.scanlines = shots[k].scan;
    dl_cfg.crt = shots[k].crt;
    dl_cfg.show_fps = shots[k].fps;
    dl_fps_shown = 60;
    snprintf(dl_shot_path, sizeof dl_shot_path, "%s" DL_SEP "gpu_%s.png", gpu_check_dir, shots[k].name);
    dl_shot_ok = false;
    bool shown = dl_gpu_show(true, false);
    printf("gpu check: %-16s %s\n", shots[k].name, shown && dl_shot_ok ? "ok" : "FAILED");
    if (!shown || !dl_shot_ok) failed++;
    dl_shot_path[0] = 0;
  }
  dl_gpu_stop();
  return failed ? 2 : 0;
}

// ---------------------------------------------------------------- main
int dl_run(const DlGame *game, int argc, char **argv) {
  dl_game = game;
  GetModuleFileNameA(NULL, dl_base_dir, MAX_PATH);
  char *slash = strrchr(dl_base_dir, DL_SEPC);
  if (slash) slash[1] = 0;
  snprintf(ini_path, sizeof ini_path, "%s" FILE_STEM ".ini", dl_base_dir);
  bool version = false;
  for (int k = 1; k < argc; k++) {
    if (!strcmp(argv[k], "--ui-audit") && k + 1 < argc) dl_audit_dir = argv[++k];
    else if (!strcmp(argv[k], "--gpu-check") && k + 1 < argc) gpu_check_dir = argv[++k];
    else if (!strcmp(argv[k], "--bench") && k + 1 < argc) bench_secs = atoi(argv[++k]);
    else if (!strcmp(argv[k], "--play")) play_now = true;
    else if (!strcmp(argv[k], "--nomouse")) dl_no_mouse = true;
    else if (!strcmp(argv[k], "--version")) version = true;
    else if (!strcmp(argv[k], "--ini") && k + 1 < argc) snprintf(ini_path, sizeof ini_path, "%s", argv[++k]);
  }
  { // started from a terminal: print there (--ui-audit reports)
    HANDLE ho = GetStdHandle(STD_OUTPUT_HANDLE);
    if ((!ho || ho == INVALID_HANDLE_VALUE || GetFileType(ho) == FILE_TYPE_UNKNOWN) && AttachConsole(ATTACH_PARENT_PROCESS)) {
      freopen("CONOUT$", "w", stdout);
      freopen("CONOUT$", "w", stderr);
    }
  }
  if (version) { // --version: DarkLauncher's, then nothing else
    printf("DarkLauncher " DL_VERSION "\n");
    fflush(stdout);
    return 0;
  }
  SetProcessDPIAware();
  dl_pic = calloc((size_t)game->width * game->height, 4);
  dl_pic_w = game->width;
  dl_pic_h = game->height;
  dl_pic_aspect = game->aspect > 0 ? game->aspect : (double)game->width / game->height;
  scan_buf = malloc((size_t)game->width * game->height * 3 * 4);
  cvd_buf = malloc((size_t)game->width * game->height * 3 * 4);
  if (!dl_pic || !scan_buf) return 1;

  // settings: the kit's, then the game's
  dl_cfg.ui_lang = dl_lang_default();
  add_setting("ui_lang", &dl_cfg.ui_lang, 0, DL_LANGS - 1, 0);
  add_setting("fullscreen", &dl_cfg.fullscreen, 0, 1, 0);
  add_setting("integer_scale", &dl_cfg.integer_scale, 0, 1, 1);
  add_setting("smooth", &dl_cfg.smooth, 0, 2, 1);
  add_setting("scanlines", &dl_cfg.scanlines, 0, 4, 1);
  add_setting("show_fps", &dl_cfg.show_fps, 0, 1, 1);
  add_setting("gpu", &dl_cfg.gpu, 0, 1, 1);
  add_setting("crt", &dl_cfg.crt, 0, 2, 1);
  add_setting("hdr", &dl_cfg.hdr, 0, 4, 1);
  add_setting("vsync", &dl_cfg.vsync, 0, 1, 1);
  add_setting("volume", &dl_cfg.volume, 0, 10, 2);
  add_setting("menu_sound", &dl_cfg.menu_sound, 0, 1, 2);
  add_setting("turbo", &dl_cfg.turbo, 0, 4, 2);
  add_setting("pause_menu", &dl_cfg.pause_menu, 0, 1, 2);
  add_setting("bg_pause", &dl_cfg.bg_pause, 0, 1, 2);
  if (game->rumble) add_setting("rumble", &dl_cfg.rumble, 0, 1, 2);
  add_setting("state_slot", &dl_slot, 0, DL_SLOTS - 1, 0);
  add_setting("a11y_contrast", &dl_cfg.contrast, 0, 1, 0); // kept by "Defaults": a player who needs them keeps them
  add_setting("a11y_cvd", &dl_cfg.cvd, 0, 3, 0);
  add_setting("a11y_still", &dl_cfg.still, 0, 1, 0);
  add_setting("a11y_big_help", &dl_cfg.big_help, 0, 1, 0);
  add_setting("win_x", &dl_cfg.win_x, -32000, 32000, 0);
  add_setting("win_y", &dl_cfg.win_y, -32000, 32000, 0);
  add_setting("win_w", &dl_cfg.win_w, 0, 32000, 0);
  add_setting("win_h", &dl_cfg.win_h, 0, 32000, 0);
  dl_input_init();
  dl_menu_init();
  if (game->setup) game->setup();
  dl_menu_finish();
  settings_load();
  if (!dl_lang_ok(dl_cfg.ui_lang)) dl_cfg.ui_lang = dl_lang_default(); // a language this game's texts do not have
  for (int k = 0; k < setting_n; k++) saved[k] = *settings[k].value; // as read: nothing to write yet
  bool crashed = !dl_audit_dir && !gpu_check_dir && !bench_secs && last_run_failed();
  if (crashed) safe_start();

  // window: where it was last time, otherwise 1280 x 800 in the middle of the screen
  WNDCLASSW wc = {0};
  wc.lpfnWndProc = wnd_proc;
  wc.hInstance = GetModuleHandleW(NULL);
  wc.lpszClassName = L"" WINDOW_CLASS;
  wc.hCursor = LoadCursorW(NULL, (LPCWSTR)IDC_ARROW);
  wc.hIcon = LoadIconW(wc.hInstance, MAKEINTRESOURCEW(1));
  RegisterClassW(&wc);
  RECT r = {0, 0, 1280, 800};
  AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, FALSE);
  int ww = r.right - r.left, wh = r.bottom - r.top;
  int wx = (GetSystemMetrics(SM_CXSCREEN) - ww) / 2, wy = (GetSystemMetrics(SM_CYSCREEN) - wh) / 2;
  if (dl_cfg.win_w >= 400 && dl_cfg.win_h >= 300 && !dl_audit_dir && !gpu_check_dir) {
    RECT want = {dl_cfg.win_x, dl_cfg.win_y, dl_cfg.win_x + dl_cfg.win_w, dl_cfg.win_y + dl_cfg.win_h};
    if (MonitorFromRect(&want, MONITOR_DEFAULTTONULL)) { wx = dl_cfg.win_x; wy = dl_cfg.win_y; ww = dl_cfg.win_w; wh = dl_cfg.win_h; }
  }
  dl_hwnd = CreateWindowW(wc.lpszClassName, L"" PROJECT_TITLE, WS_OVERLAPPEDWINDOW, wx, wy, ww, wh, NULL, NULL, wc.hInstance, NULL);
  if (!dl_hwnd) return 1;
  if (dl_audit_dir) {
    // the picture of a first frame, then every page; nothing is saved
    if (game->start) game->start();
    dl_start_done = true;
    static s16 tmp[8192];
    game->frame(&(DlInput){0}, dl_pic, tmp, 4096);
    ShowWindow(dl_hwnd, SW_SHOWNOACTIVATE);
    return dl_audit();
  }
  if (gpu_check_dir) { // nothing is saved
    if (game->start && !game->start()) return 1;
    dl_start_done = true;
    return gpu_check();
  }
  if (!bench_secs) run_mark_write(); // removed when the program closes normally
  if (dl_cfg.gpu) dl_gpu_prepare(); // the shaders, on a second thread; GDI shows the first pictures
  ShowWindow(dl_hwnd, SW_SHOW);
  bool want_full = dl_cfg.fullscreen;
  dl_cfg.fullscreen = 0;
  if (want_full) dl_toggle_fullscreen();
  if (game->start && !game->start()) { run_mark_clear(); return 1; } // the game said why: not a crash
  dl_start_done = true;
  ui_window_icon();
  audio_init();
  wait_start();

  LARGE_INTEGER fq, now, next, fps_t0;
  QueryPerformanceFrequency(&fq);
  QueryPerformanceCounter(&next);
  fps_t0 = next;
  int fps_frames = 0;
  double period = fq.QuadPart / (game->fps > 0 ? game->fps : 60.0);
  int audio_max = game->audio_rate > 0 ? (int)(game->audio_rate / (game->fps > 0 ? game->fps : 60.0)) * 2 + 64 : 0;
  s16 *audio = audio_max ? malloc((size_t)audio_max * 4) : NULL;
  u32 bench_frames = 0, bench_gaps = 0;
  if (crashed) dl_safe_page_open();
  else if (bench_secs || play_now) dl_play();
  LARGE_INTEGER bench_t0, menu_next = {0}; // menu_next: when the sky behind the setup screen moves next
  QueryPerformanceCounter(&bench_t0);
  while (dl_running) {
    if (bench_secs) {
      QueryPerformanceCounter(&now);
      double t = (double)(now.QuadPart - bench_t0.QuadPart) / (double)fq.QuadPart;
      if (t >= bench_secs) {
        printf("bench: %u pictures in %.1f s (%.2f per second), sound gaps %u\n", bench_frames, t, bench_frames / t, bench_gaps);
        break;
      }
    }
    MSG msg;
    while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) {
      TranslateMessage(&msg);
      DispatchMessageW(&msg);
    }
    if (!dl_running) break;
    dl_pad_poll();
    dl_gpu_follow(false);
    if (!bench_secs) settings_follow();
    { // a note that has just ended goes away
      static bool noted;
      bool now_noted = dl_note_now() != NULL;
      if (noted && !now_noted) dl_dirty = true;
      noted = now_noted;
    }
    // rewind: its key held while playing goes back, a step every 90 ms
    if (dl_mode == M_GAME && dl_started && dl_rewind_ok() && GetForegroundWindow() == dl_hwnd && GetAsyncKeyState(dl_hot_vk[HK_REWIND]) < 0) {
      if (dl_rewind_step() && !(dl_gpu_ready() && dl_gpu_show(false, false))) {
        HDC dc = GetDC(dl_hwnd);
        present(dc);
        ReleaseDC(dl_hwnd, dc);
      }
      audio_pump();
      Sleep(2);
      QueryPerformanceCounter(&next);
      continue;
    }
    dl_rewind_end();
    // the game runs while it is played, and, before it is started, behind the setup screen (its own
    // title or demo shows in the preview); once started, the setup screen pauses it, and so may
    // another window in front (not in a bench: it measures the game played, whatever window is in front)
    bool run = dl_mode == M_GAME || !dl_started, bg_pause = dl_cfg.bg_pause && !bench_secs;
    if (dl_mode == M_GAME && bg_pause && GetForegroundWindow() != dl_hwnd) run = false;
    // a window put away in the taskbar: nothing is drawn, and with "Pause in background" the game
    // waits too (its title behind the setup screen as well)
    bool hidden = IsIconic(dl_hwnd);
    if (hidden && bg_pause) run = false;
    // fast forward: several pictures of the game for one, without their sound
    bool turbo = dl_mode == M_GAME && (dl_turbo_held() || (dl_started && game->turbo && game->turbo()));
    int speed = !turbo ? 1 : dl_cfg.turbo == 2 ? 2 : dl_cfg.turbo == 4 ? 4 : 12;
    // the wait for the screen refresh is skipped whenever the sound is running short: sound first
    int queued = wave_out ? audio_queued() : 0;
    bool sound_ok = !wave_out || queued >= 3;
    // paced by the sound: a picture whenever fewer than three buffers (about 50 ms) wait to be played
    bool pace = game->pace_by_sound && wave_out && !turbo;
    // smooth motion: each picture takes several screen refreshes, so the screen sets the pace; the
    // sound, a little short then, is stretched by a few samples, which cannot be heard
    int steps = dl_mode == M_GAME && !turbo && !hidden && dl_gpu_ready() ? dl_gpu_steps() : 1;
    if (steps > 1) pace = false;
    audio_stretch = steps > 1 && wave_out && queued < 3 ? 8 : 0;
    if (bench_frames > 120 && wave_out && queued == 0 && run) bench_gaps++; // the sound card ran dry
    QueryPerformanceCounter(&now);
    if (run && (steps > 1 || (pace ? queued < 3 : now.QuadPart >= next.QuadPart))) {
      DlInput in = {dl_mode == M_GAME ? dl_game_buttons() : 0};
      for (int k = 0; k < speed; k++) {
        int n = game->frame(&in, dl_pic, audio, audio_max);
        if (n > 0 && audio && !turbo) audio_add(audio, n);
        if (dl_mode == M_GAME && dl_started) dl_rewind_snap();
        fps_frames++;
        bench_frames++;
      }
      next.QuadPart += (LONGLONG)period;
      if (now.QuadPart - next.QuadPart > (LONGLONG)(period * 4)) next = now; // far behind: no catching up
      if (now.QuadPart - fps_t0.QuadPart >= fq.QuadPart) { dl_fps_shown = fps_frames; fps_frames = 0; fps_t0 = now; }
      bool vsync = dl_cfg.vsync && dl_mode == M_GAME && sound_ok && !turbo;
      if (hidden) { // minimized: nothing drawn
      } else if (dl_gpu_ready() && dl_gpu_show(dl_dirty, vsync)) dl_dirty = false;
      else {
        HDC dc = GetDC(dl_hwnd);
        if (dl_mode == M_GAME) present(dc);
        else if (dl_dirty) { dl_draw_menu(dc); dl_dirty = false; }
        else dl_draw_preview(dc);
        ReleaseDC(dl_hwnd, dc);
        if (vsync) DwmFlush();
      }
    } else if (hidden) { // minimized: nothing drawn
    } else if (!run && dl_gpu_ready()) {
      // the setup screen over a paused game: the sky moves at 60 pictures a second at most, however
      // fast the screen; a change shows at once
      QueryPerformanceCounter(&now);
      if (dl_dirty || now.QuadPart >= menu_next.QuadPart) {
        menu_next.QuadPart = now.QuadPart + (LONGLONG)(fq.QuadPart / 60.0) - fq.QuadPart / 1000; // a little early: the refresh after it
        if (dl_gpu_show(dl_dirty, true)) dl_dirty = false;
      }
    } else if (!run && dl_dirty) {
      HDC dc = GetDC(dl_hwnd);
      dl_draw_menu(dc);
      dl_dirty = false;
      ReleaseDC(dl_hwnd, dc);
    }
    audio_pump();
    QueryPerformanceCounter(&now);
    if (run && pace && audio_event) { // paced by the sound: until a buffer has been played (one lasts 10 to 16 ms)
      MsgWaitForMultipleObjects(1, &audio_event, FALSE, (DWORD)(ABUF_FRAMES * 1000 / dl_game->audio_rate + 2), QS_ALLINPUT);
      continue;
    }
    double left_ms = run ? (steps > 1 ? 0 : pace ? 2 : (next.QuadPart - now.QuadPart) * 1000.0 / fq.QuadPart)
                   : hidden ? 100
                   : dl_gpu_ready() ? (menu_next.QuadPart - now.QuadPart) * 1000.0 / fq.QuadPart
                   : 10;
    wait_ms(left_ms, run);
  }
  wait_stop();
  if (bench_secs) { dl_gpu_stop(); if (wave_out) { waveOutReset(wave_out); waveOutClose(wave_out); } return 0; } // nothing is saved
  if (!is_fullscreen) {
    RECT wr;
    if (GetWindowRect(dl_hwnd, &wr)) { dl_cfg.win_x = wr.left; dl_cfg.win_y = wr.top; dl_cfg.win_w = wr.right - wr.left; dl_cfg.win_h = wr.bottom - wr.top; }
  }
  dl_rumble_stop();
  dl_session_keep();
  if (game->quit) game->quit();
  settings_save();
  dl_gpu_stop();
  if (wave_out) { waveOutReset(wave_out); waveOutClose(wave_out); }
  run_mark_clear(); // a normal close
  if (relaunch) { // the same program with the same command line, once this window is gone
    DestroyWindow(dl_hwnd);
    wchar_t exe[MAX_PATH];
    STARTUPINFOW si = {sizeof si};
    PROCESS_INFORMATION pi;
    GetModuleFileNameW(NULL, exe, MAX_PATH);
    if (CreateProcessW(exe, GetCommandLineW(), NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
      CloseHandle(pi.hThread);
      CloseHandle(pi.hProcess);
    }
  }
  return 0;
}
