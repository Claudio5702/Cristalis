// The program around the game: settings file, window and fullscreen, the main loop (one game frame
// at the game's rate), sound output, keyboard and controller, the picture with its filters, menu
// sounds and the command line (--ui-audit DIR, --gpu-check DIR, --nomouse, --ini FILE).
#include "dl_int.h"
#include <math.h>
#include <mmsystem.h>
#include <dwmapi.h>
#include <time.h>
#ifndef _WIN32
#include <sys/utsname.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <dlfcn.h>
#include <dirent.h>
#include <fnmatch.h>
#include <spawn.h>
#include <sys/stat.h>
#include <sys/wait.h>
extern char **environ;
#endif

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

static bool first_start; // no settings file yet: the first start's pages

static void settings_load(void) {
  FILE *f = fopen(ini_path, "r");
  first_start = !f;
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

// The marker also says, every ten seconds, where the player is (and the game's line, DlGame.report);
// an error that ends the program adds itself to it. The next run writes the crash report from it.
static time_t run_started;

static void now_text(char *out, int n, time_t t) {
  struct tm tm;
#ifdef _WIN32
  localtime_s(&tm, &t);
#else
  localtime_r(&t, &tm);
#endif
  strftime(out, (size_t)n, "%Y-%m-%d %H:%M:%S", &tm);
}

static void run_mark_write(void) {
  FILE *f = fopen(run_mark, "w");
  if (!f) return;
  if (!run_started) run_started = time(NULL);
  char started[32], seen[32], game_line[200] = "";
  now_text(started, sizeof started, run_started);
  now_text(seen, sizeof seen, time(NULL));
  if (dl_game->report && dl_start_done) dl_game->report(game_line, sizeof game_line - 1);
  game_line[sizeof game_line - 1] = 0;
  for (char *c = game_line; *c; c++)
    if (*c == '\n' || *c == '\r') *c = ' ';
  const char *where = dl_mode == M_GAME ? "playing" : dl_mode == M_PAUSE ? "pause menu" : dl_mode == M_PAGE ? "setup screen, a page" : "setup screen";
  char page[24] = "";
  if (dl_mode == M_PAGE) snprintf(page, sizeof page, " %d", dl_cur);
  fprintf(f, "%lu\nstarted %s\nlast seen %s\nwhere %s%s%s\nplayed this run %ld s\ngame %s\n", (unsigned long)GetCurrentProcessId(), started, seen,
          where, page, dl_started ? "" : ", game not started yet", (long)(time(NULL) - run_started), game_line);
  fclose(f);
}

// An error that ends the program: what and where, added to the marker; then the filter that was there
// before (the game's, if it set one), or what Windows does as usual. The line is written without the C
// library's heap or its FILE locks: the error may have come from inside them.
static LPTOP_LEVEL_EXCEPTION_FILTER crash_before;
static LONG WINAPI crash_filter(EXCEPTION_POINTERS *ep) {
  if (ep && ep->ExceptionRecord) {
    void *at = ep->ExceptionRecord->ExceptionAddress;
    HMODULE m = NULL;
    char mod[MAX_PATH] = "?", line[MAX_PATH + 64];
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (const char *)at, &m) && m)
      GetModuleFileNameA(m, mod, sizeof mod);
    const char *leaf = strrchr(mod, DL_SEPC);
#ifdef _WIN32
    const char *eol = "\r\n"; // as the rest of the marker, written as text
#else
    const char *eol = "\n";
#endif
    int n = snprintf(line, sizeof line, "error 0x%08lX at %s+0x%llX%s", (unsigned long)ep->ExceptionRecord->ExceptionCode, leaf ? leaf + 1 : mod,
                     (unsigned long long)((uintptr_t)at - (uintptr_t)m), eol);
    if (n >= (int)sizeof line) n = (int)sizeof line - 1;
#ifdef _WIN32
    HANDLE h = CreateFileA(run_mark, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h != INVALID_HANDLE_VALUE) {
      DWORD done;
      if (n > 0) WriteFile(h, line, (DWORD)n, &done, NULL);
      CloseHandle(h);
    }
#else
    int fd = open(run_mark, O_WRONLY | O_APPEND);
    if (fd >= 0) {
      if (n > 0 && write(fd, line, (size_t)n) < 0) {}
      close(fd);
    }
#endif
  }
  return crash_before ? crash_before(ep) : EXCEPTION_CONTINUE_SEARCH;
}

// ---- the crash report: "<settings file>.crash.txt", written at the start of the run after one that
// did not close normally, from the marker it left
static char report_path[MAX_PATH], *report_text;

const char *dl_report_file(void) { return report_path; }

static const char *error_name(unsigned long code) {
  switch (code) {
    case 0xC0000005: return "access violation";
    case 0xC000001D: return "illegal instruction";
    case 0xC0000094: return "integer division by zero";
    case 0xC00000FD: return "stack overflow";
    case 0xC0000409: return "stack buffer overrun";
    case 0x40000015: return "aborted";
    default: return "";
  }
}

static void report_write(void) {
  size_t cap = 65536, n = 0;
  char *out = malloc(cap);
  if (!out) return;
  #define ADD(...) do { int w_ = snprintf(out + n, cap - n, __VA_ARGS__); if (w_ > 0) n += (size_t)w_ < cap - n ? (size_t)w_ : cap - n - 1; } while (0)
  char now[32], line[512];
  now_text(now, sizeof now, time(NULL));
  ADD("%s (%s): crash report\nDarkLauncher " DL_VERSION "\nWritten %s\n", PROJECT_NAME, GAME_NAME, now);
#ifdef _WIN32
  typedef LONG(WINAPI * RtlGetVersionFn)(OSVERSIONINFOW *);
  RtlGetVersionFn rtl = (RtlGetVersionFn)(void *)GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlGetVersion");
  OSVERSIONINFOW v = {sizeof v};
  if (rtl && !rtl(&v)) ADD("Windows %lu.%lu build %lu\n", v.dwMajorVersion, v.dwMinorVersion, v.dwBuildNumber);
#else
  struct utsname u;
  if (!uname(&u)) ADD("%s %s %s\n", u.sysname, u.release, u.machine);
#endif
  ADD("\nThe last run:\n");
  bool error = false;
  FILE *f = fopen(run_mark, "r");
  if (f) {
    bool first = true;
    while (fgets(line, sizeof line, f)) {
      if (first) { first = false; continue; } // its process id
      unsigned long code;
      if (sscanf(line, "error 0x%lX", &code) == 1) {
        error = true;
        line[strcspn(line, "\r\n")] = 0;
        ADD("  %s (%s)\n", line, error_name(code));
      } else ADD("  %s", line);
    }
    fclose(f);
  }
  if (!error)
    ADD("  no error was recorded: the program was ended from outside (the system, a driver, the power, closed by force...),\n"
        "  or by the game's own error handler, which did not hand the error on to DarkLauncher\n");
  ADD("\nSettings (%s):\n", ini_path);
  f = fopen(ini_path, "r");
  if (f) {
    while (fgets(line, sizeof line, f)) ADD("  %s", line);
    fclose(f);
  }
  #undef ADD
  snprintf(report_path, sizeof report_path, "%s.crash.txt", ini_path);
  f = fopen(report_path, "wb");
  if (f) { fwrite(out, 1, n, f); fclose(f); }
  free(report_text);
  report_text = out;
}

// "Copy the crash report": into the clipboard, as text.
void dl_report_copy(void) {
  if (!report_text) return;
  int wn = MultiByteToWideChar(CP_UTF8, 0, report_text, -1, NULL, 0);
  HANDLE h = wn > 0 ? GlobalAlloc(GMEM_MOVEABLE, (size_t)wn * sizeof(wchar_t)) : NULL;
  wchar_t *w = h ? GlobalLock(h) : NULL;
  if (!w) return;
  MultiByteToWideChar(CP_UTF8, 0, report_text, -1, w, wn);
  GlobalUnlock(h);
  if (OpenClipboard(dl_hwnd)) {
    EmptyClipboard();
    SetClipboardData(CF_UNICODETEXT, h);
    CloseClipboard();
    dl_note(TX(T_M_REPORT));
  }
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

int dl_crt(void) { return dl_game && (dl_game->hide & DL_HIDE_CRT) ? 0 : dl_cfg.crt; }
int dl_scanlines(void) { return dl_game && (dl_game->hide & DL_HIDE_SCANLINES) ? 0 : dl_cfg.scanlines; }

// The game's picture into a rectangle, with the filter and the scanlines; the frame rate on top.
void dl_blit(HDC dc, int x, int y, int w, int h) {
  if (w <= 0 || h <= 0 || !dl_pic) return;
  int gw = dl_pic_w, gh = dl_pic_h, sh = gh;
  const u32 *src = dl_pic;
  if (dl_scanlines()) {
    // each line three times, the third one darker
    u32 keep = (u32)(256 - dl_scanlines() * 48);
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

// ---------------------------------------------------------------- online, both off at first: the
// player's status on Discord (its local pipe; DISCORD_APP_ID), the look for a newer version at start
// (UPDATE_URL, through WinHTTP or libcurl, loaded when needed: nothing more to link)
static char *json_text(char *out, size_t cap, const char *s) { // s as a JSON string's content
  size_t n = 0;
  for (; *s && n + 7 < cap; s++) {
    unsigned char c = (unsigned char)*s;
    if (c == '"' || c == '\\') { out[n++] = '\\'; out[n++] = (char)c; }
    else if (c < 32) n += (size_t)snprintf(out + n, cap - n, "\\u%04x", c);
    else out[n++] = (char)c;
  }
  out[n] = 0;
  return out;
}

static void utf8_of(const wchar_t *w, char *out, int cap) {
  if (WideCharToMultiByte(CP_UTF8, 0, w, -1, out, cap, NULL, NULL) <= 0) out[0] = 0;
}

#ifdef _WIN32
static HANDLE ds_pipe = INVALID_HANDLE_VALUE;
static bool ds_up(void) { return ds_pipe != INVALID_HANDLE_VALUE; }
static void ds_close(void) { if (ds_up()) CloseHandle(ds_pipe); ds_pipe = INVALID_HANDLE_VALUE; }
static bool ds_connect(void) {
  for (int k = 0; k < 10 && !ds_up(); k++) {
    char name[MAX_PATH];
    const char *test = getenv("DL_DISCORD_PIPE"); // tests: another pipe than Discord's
    if (test) snprintf(name, sizeof name, "\\\\.\\pipe\\%s", test);
    else snprintf(name, sizeof name, "\\\\.\\pipe\\discord-ipc-%d", k);
    ds_pipe = CreateFileA(name, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (test) break;
  }
  return ds_up();
}
static bool ds_write(const void *p, size_t n) {
  DWORD w = 0;
  if (WriteFile(ds_pipe, p, (DWORD)n, &w, NULL) && w == n) return true;
  ds_close();
  return false;
}
static void ds_drain(void) { // Discord's answers, read and left
  DWORD avail = 0;
  while (ds_up() && PeekNamedPipe(ds_pipe, NULL, 0, NULL, &avail, NULL) && avail) {
    char b[1024];
    DWORD got = 0;
    if (!ReadFile(ds_pipe, b, avail < sizeof b ? avail : sizeof b, &got, NULL)) ds_close();
  }
  if (ds_up() && !PeekNamedPipe(ds_pipe, NULL, 0, NULL, &avail, NULL)) ds_close(); // Discord has gone
}
#else
static int ds_fd = -1;
static bool ds_up(void) { return ds_fd >= 0; }
static void ds_close(void) { if (ds_fd >= 0) close(ds_fd); ds_fd = -1; }
static bool ds_connect(void) {
  const char *dir = getenv("XDG_RUNTIME_DIR"), *test = getenv("DL_DISCORD_PIPE");
  for (int k = 0; k < 10 && !ds_up(); k++) {
    struct sockaddr_un a = {.sun_family = AF_UNIX};
    if (test) snprintf(a.sun_path, sizeof a.sun_path, "%s", test);
    else snprintf(a.sun_path, sizeof a.sun_path, "%s/discord-ipc-%d", dir ? dir : "/tmp", k);
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) return false;
    if (connect(fd, (struct sockaddr *)&a, sizeof a)) { close(fd); if (test) break; continue; }
    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK);
    ds_fd = fd;
  }
  return ds_up();
}
static bool ds_write(const void *p, size_t n) {
  if (send(ds_fd, p, n, MSG_NOSIGNAL) == (ssize_t)n) return true;
  ds_close();
  return false;
}
static void ds_drain(void) {
  char b[1024];
  for (;;) {
    ssize_t got = recv(ds_fd, b, sizeof b, 0);
    if (got > 0) continue;
    if (got == 0 || (errno != EAGAIN && errno != EWOULDBLOCK)) ds_close();
    return;
  }
}
#endif

static bool ds_frame(int op, const char *json) { // Discord's frames: an opcode, a length, the JSON
  size_t n = strlen(json);
  char *buf = malloc(n + 8);
  if (!buf) return false;
  uint32_t head[2] = {(uint32_t)op, (uint32_t)n};
  memcpy(buf, head, 8);
  memcpy(buf + 8, json, n);
  bool ok = ds_write(buf, n + 8);
  free(buf);
  return ok;
}

static time_t online_started;

static void discord_follow(bool now) {
  static DWORD last;
  static int last_mode = -1;
  if (!DISCORD_APP_ID[0]) return;
  if (!dl_cfg.discord) { ds_close(); return; }
  if (!now && GetTickCount() - last < 15000 && dl_mode == last_mode) return; // Discord takes a status every 15 s at most
  if (!now && GetTickCount() - last < 4000) return;
  last = GetTickCount();
  last_mode = dl_mode;
  if (!ds_up()) {
    if (!ds_connect()) return;
    char hello[160];
    snprintf(hello, sizeof hello, "{\"v\":1,\"client_id\":\"%s\"}", DISCORD_APP_ID);
    if (!ds_frame(0, hello)) return;
  }
  ds_drain();
  if (!ds_up()) return;
  if (!online_started) online_started = time(NULL);
  char status[256] = "", where[96], a[600], b[256], json[1400];
  if (dl_game->status && dl_start_done) dl_game->status(status, sizeof status - 1);
  status[sizeof status - 1] = 0;
  utf8_of(TX(dl_mode == M_GAME ? T_DS_PLAYING : T_DS_MENUS), where, sizeof where);
  static unsigned nonce;
  snprintf(json, sizeof json,
           "{\"cmd\":\"SET_ACTIVITY\",\"args\":{\"pid\":%lu,\"activity\":{\"details\":\"%s\",\"state\":\"%s\",\"timestamps\":{\"start\":%lld}}},\"nonce\":\"%u\"}",
           (unsigned long)GetCurrentProcessId(), json_text(a, sizeof a, status[0] ? status : where), json_text(b, sizeof b, status[0] ? where : PROJECT_NAME),
           (long long)online_started, ++nonce);
  ds_frame(1, json);
}

void dl_discord_changed(void) { discord_follow(true); }

// ---- a newer version: UPDATE_URL, or the newest release on GitHub (UPDATE_GITHUB), read on a thread of
// its own once at start; with UPDATE_ASSET, "Update" downloads the release's file, checks its size and
// its SHA-256 (as GitHub gives them) and installs it
static volatile LONG upd_state; // 0 nothing yet, 1 looking, 2 a newer version, 3 none newer, 4 no answer (a private repository, no network)
static char upd_version[32], upd_page[512];
static char upd_asset_url[1024];      // the release's file for this system (UPDATE_ASSET), if it may be installed
static long long upd_asset_size;
static unsigned char upd_asset_sha[32];
static volatile LONG upd_get; // 0 not asked, 1 under way, 2 checked (and on Linux put in place), 3 failed
static volatile LONG upd_pct;

// A GET: the body to sink (false from it stops), true when it all came with "200 OK". The User-Agent
// is the project's name (GitHub refuses a request without one). long_wait: a download, limited only
// when the connection stalls.
typedef bool (*HttpSink)(void *ctx, const char *data, int n);
#ifndef _WIN32
typedef struct { HttpSink sink; void *ctx; } CurlSink;
static size_t http_take(char *p, size_t size, size_t n, void *ctx) { // libcurl's write callback
  CurlSink *c = ctx;
  return c->sink(c->ctx, p, (int)(size * n)) ? size * n : 0;
}
#endif
static bool http_fetch(const char *url, HttpSink sink, void *ctx, bool long_wait) {
  bool ok = false;
#ifdef _WIN32
  typedef void *HI;
  typedef HI(WINAPI * OpenFn)(LPCWSTR, DWORD, LPCWSTR, LPCWSTR, DWORD);
  typedef BOOL(WINAPI * CrackFn)(LPCWSTR, DWORD, DWORD, void *);
  typedef HI(WINAPI * ConnectFn)(HI, LPCWSTR, WORD, DWORD);
  typedef HI(WINAPI * RequestFn)(HI, LPCWSTR, LPCWSTR, LPCWSTR, LPCWSTR, LPCWSTR *, DWORD);
  typedef BOOL(WINAPI * SendFn)(HI, LPCWSTR, DWORD, void *, DWORD, DWORD, DWORD_PTR);
  typedef BOOL(WINAPI * ReceiveFn)(HI, void *);
  typedef BOOL(WINAPI * QueryFn)(HI, DWORD, LPCWSTR, void *, DWORD *, DWORD *);
  typedef BOOL(WINAPI * ReadFn)(HI, void *, DWORD, DWORD *);
  typedef BOOL(WINAPI * CloseFn)(HI);
  typedef BOOL(WINAPI * TimeoutsFn)(HI, int, int, int, int);
  HMODULE w = LoadLibraryA("winhttp.dll");
  if (!w) return false;
  OpenFn h_open = (OpenFn)(void *)GetProcAddress(w, "WinHttpOpen");
  CrackFn h_crack = (CrackFn)(void *)GetProcAddress(w, "WinHttpCrackUrl");
  ConnectFn h_connect = (ConnectFn)(void *)GetProcAddress(w, "WinHttpConnect");
  RequestFn h_request = (RequestFn)(void *)GetProcAddress(w, "WinHttpOpenRequest");
  SendFn h_send = (SendFn)(void *)GetProcAddress(w, "WinHttpSendRequest");
  ReceiveFn h_receive = (ReceiveFn)(void *)GetProcAddress(w, "WinHttpReceiveResponse");
  QueryFn h_query = (QueryFn)(void *)GetProcAddress(w, "WinHttpQueryHeaders");
  ReadFn h_read = (ReadFn)(void *)GetProcAddress(w, "WinHttpReadData");
  CloseFn h_close = (CloseFn)(void *)GetProcAddress(w, "WinHttpCloseHandle");
  TimeoutsFn h_timeouts = (TimeoutsFn)(void *)GetProcAddress(w, "WinHttpSetTimeouts");
  if (!h_open || !h_crack || !h_connect || !h_request || !h_send || !h_receive || !h_query || !h_read || !h_close) return false;
  wchar_t wurl[1024], host[256], path[2048], extra[1024];
  MultiByteToWideChar(CP_UTF8, 0, url, -1, wurl, 1024);
  struct { DWORD size; LPWSTR scheme; DWORD scheme_n; int scheme_kind; LPWSTR host; DWORD host_n; WORD port; LPWSTR user; DWORD user_n; LPWSTR pass;
           DWORD pass_n; LPWSTR path; DWORD path_n; LPWSTR extra; DWORD extra_n; } uc = {sizeof uc};
  uc.host = host, uc.host_n = 256, uc.path = path, uc.path_n = 1024, uc.scheme_n = (DWORD)-1, uc.extra = extra, uc.extra_n = 1024;
  if (!h_crack(wurl, 0, 0, &uc)) return false;
  wcsncat(path, extra, 2047 - wcslen(path)); // "?..." after the path
  HI s = h_open(L"" PROJECT_NAME, 0, NULL, NULL, 0), c = NULL, r = NULL;
  if (s && h_timeouts) h_timeouts(s, 5000, 5000, 5000, long_wait ? 30000 : 5000);
  if (s) c = h_connect(s, host, uc.port, 0);
  if (c) r = h_request(c, L"GET", path, NULL, NULL, NULL, uc.scheme_kind == 2 ? 0x00800000 /* WINHTTP_FLAG_SECURE */ : 0);
  DWORD code = 0, code_n = sizeof code;
  if (r && h_send(r, L"Accept: application/vnd.github+json\r\n", (DWORD)-1, NULL, 0, 0, 0) && h_receive(r, NULL) &&
      h_query(r, 19 | 0x20000000 /* WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER */, NULL, &code, &code_n, NULL) && code == 200) {
    static char buf[1 << 16];
    DWORD got;
    ok = true;
    while (ok && h_read(r, buf, sizeof buf, &got) && got) ok = sink(ctx, buf, (int)got);
  }
  if (r) h_close(r);
  if (c) h_close(c);
  if (s) h_close(s);
#else
  typedef void CURL;
  void *lib = dlopen("libcurl.so.4", RTLD_NOW);
  if (!lib) return false;
  CURL *(*easy_init)(void) = (CURL * (*)(void)) dlsym(lib, "curl_easy_init");
  int (*easy_setopt)(CURL *, int, ...) = (int (*)(CURL *, int, ...))dlsym(lib, "curl_easy_setopt");
  int (*easy_perform)(CURL *) = (int (*)(CURL *))dlsym(lib, "curl_easy_perform");
  void (*easy_cleanup)(CURL *) = (void (*)(CURL *))dlsym(lib, "curl_easy_cleanup");
  CURL *h = easy_init && easy_setopt && easy_perform && easy_cleanup ? easy_init() : NULL;
  if (h) {
    CurlSink cs = {sink, ctx};
    size_t (*take)(char *, size_t, size_t, void *) = http_take;
    easy_setopt(h, 10002 /* CURLOPT_URL */, url);
    easy_setopt(h, 10018 /* CURLOPT_USERAGENT */, PROJECT_NAME);
    easy_setopt(h, 20011 /* CURLOPT_WRITEFUNCTION */, take);
    easy_setopt(h, 10001 /* CURLOPT_WRITEDATA */, &cs);
    easy_setopt(h, 45 /* CURLOPT_FAILONERROR */, 1L);
    if (long_wait) {
      easy_setopt(h, 19 /* CURLOPT_LOW_SPEED_LIMIT */, 1L);
      easy_setopt(h, 20 /* CURLOPT_LOW_SPEED_TIME */, 30L);
    } else easy_setopt(h, 13 /* CURLOPT_TIMEOUT */, 10L);
    easy_setopt(h, 52 /* CURLOPT_FOLLOWLOCATION */, 1L);
    easy_setopt(h, 99 /* CURLOPT_NOSIGNAL */, 1L);
    ok = !easy_perform(h);
    easy_cleanup(h);
  }
#endif
  return ok;
}

typedef struct { char *out; int cap, n; } MemSink;
static bool mem_sink(void *ctx, const char *d, int n) { // as much as fits
  MemSink *m = ctx;
  int room = m->cap - m->n;
  memcpy(m->out + m->n, d, (size_t)(n < room ? n : room));
  m->n += n < room ? n : room;
  return true;
}
static int http_get(const char *url, char *out, int cap) { // the body, or -1
  MemSink m = {out, cap, 0};
  return http_fetch(url, mem_sink, &m, false) ? m.n : -1;
}

// ---- SHA-256 (FIPS 180-4), for the release's file
typedef struct { uint32_t h[8]; uint64_t len; unsigned char buf[64]; int n; } Sha256;
static const uint32_t sha_k[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
    0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
    0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
    0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};
#define SHA_ROR(x, n) (((x) >> (n)) | ((x) << (32 - (n))))
static void sha_block(Sha256 *s, const unsigned char *p) {
  uint32_t w[64], v[8];
  for (int k = 0; k < 16; k++) w[k] = (uint32_t)p[4 * k] << 24 | (uint32_t)p[4 * k + 1] << 16 | (uint32_t)p[4 * k + 2] << 8 | p[4 * k + 3];
  for (int k = 16; k < 64; k++) {
    uint32_t s0 = SHA_ROR(w[k - 15], 7) ^ SHA_ROR(w[k - 15], 18) ^ (w[k - 15] >> 3), s1 = SHA_ROR(w[k - 2], 17) ^ SHA_ROR(w[k - 2], 19) ^ (w[k - 2] >> 10);
    w[k] = w[k - 16] + s0 + w[k - 7] + s1;
  }
  memcpy(v, s->h, sizeof v);
  for (int k = 0; k < 64; k++) {
    uint32_t t1 = v[7] + (SHA_ROR(v[4], 6) ^ SHA_ROR(v[4], 11) ^ SHA_ROR(v[4], 25)) + ((v[4] & v[5]) ^ (~v[4] & v[6])) + sha_k[k] + w[k];
    uint32_t t2 = (SHA_ROR(v[0], 2) ^ SHA_ROR(v[0], 13) ^ SHA_ROR(v[0], 22)) + ((v[0] & v[1]) ^ (v[0] & v[2]) ^ (v[1] & v[2]));
    memmove(v + 1, v, 7 * sizeof v[0]);
    v[4] += t1;
    v[0] = t1 + t2;
  }
  for (int k = 0; k < 8; k++) s->h[k] += v[k];
}
static void sha_init(Sha256 *s) {
  static const uint32_t h0[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
  memcpy(s->h, h0, sizeof h0);
  s->len = 0;
  s->n = 0;
}
static void sha_add(Sha256 *s, const void *data, size_t n) {
  const unsigned char *p = data;
  s->len += n;
  while (n) {
    size_t take = 64 - (size_t)s->n < n ? 64 - (size_t)s->n : n;
    memcpy(s->buf + s->n, p, take);
    s->n += (int)take, p += take, n -= take;
    if (s->n == 64) { sha_block(s, s->buf); s->n = 0; }
  }
}
static void sha_end(Sha256 *s, unsigned char out[32]) {
  uint64_t bits = s->len * 8;
  unsigned char pad = 0x80, zero = 0, len[8];
  sha_add(s, &pad, 1);
  while (s->n != 56) sha_add(s, &zero, 1);
  for (int k = 0; k < 8; k++) len[k] = (unsigned char)(bits >> (56 - 8 * k));
  sha_add(s, len, 8);
  for (int k = 0; k < 8; k++) out[4 * k] = (unsigned char)(s->h[k] >> 24), out[4 * k + 1] = (unsigned char)(s->h[k] >> 16),
                               out[4 * k + 2] = (unsigned char)(s->h[k] >> 8), out[4 * k + 3] = (unsigned char)s->h[k];
}

// ---- the little JSON GitHub's answer needs: the members of an object, one level down
static const char *json_ws(const char *p) { while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++; return p; }
static const char *json_skip(const char *p) { // past one value
  p = json_ws(p);
  if (*p == '"') {
    for (p++; *p && *p != '"'; p++)
      if (*p == '\\' && p[1]) p++;
    return *p ? p + 1 : p;
  }
  if (*p == '{' || *p == '[') {
    int depth = 0;
    for (; *p; p++) {
      if (*p == '"') { p = json_skip(p) - 1; continue; }
      if (*p == '{' || *p == '[') depth++;
      else if ((*p == '}' || *p == ']') && --depth == 0) return p + 1;
    }
    return p;
  }
  while (*p && *p != ',' && *p != '}' && *p != ']') p++;
  return p;
}
static void json_str(const char *v, char *out, int cap) { // a string value, its escapes undone (\uXXXX: ASCII only)
  int n = 0;
  v = json_ws(v);
  if (*v++ != '"') { out[0] = 0; return; }
  for (; *v && *v != '"' && n < cap - 1; v++) {
    char c = *v;
    if (c == '\\' && v[1]) {
      c = *++v;
      if (c == 'n') c = '\n';
      else if (c == 't') c = '\t';
      else if (c == 'u' && v[1] && v[2] && v[3] && v[4]) { c = (char)strtol((char[]){v[1], v[2], v[3], v[4], 0}, NULL, 16); v += 4; }
    }
    out[n++] = c;
  }
  out[n] = 0;
}
// Calls found(key, value) for each member of the object at p ('{'); returns past it.
static const char *json_members(const char *p, void (*found)(const char *key, const char *value, void *ctx), void *ctx) {
  p = json_ws(p);
  if (*p != '{') return json_skip(p);
  p++;
  for (;;) {
    p = json_ws(p);
    if (*p != '"') break;
    char key[64];
    json_str(p, key, sizeof key);
    p = json_ws(json_skip(p));
    if (*p++ != ':') break;
    const char *v = json_ws(p);
    found(key, v, ctx);
    p = json_ws(json_skip(v));
    if (*p == ',') p++;
    else break;
  }
  p = json_ws(p);
  return *p == '}' ? p + 1 : p;
}

typedef struct { char name[256], url[1024], digest[96]; long long size; } Asset;
typedef struct { char tag[64], page[512]; Asset pick; bool picked; } Release;
static void asset_member(const char *key, const char *v, void *ctx) {
  Asset *a = ctx;
  if (!strcmp(key, "name")) json_str(v, a->name, sizeof a->name);
  else if (!strcmp(key, "browser_download_url")) json_str(v, a->url, sizeof a->url);
  else if (!strcmp(key, "digest")) json_str(v, a->digest, sizeof a->digest);
  else if (!strcmp(key, "size")) a->size = strtoll(v, NULL, 10);
}
static void release_member(const char *key, const char *v, void *ctx) {
  Release *r = ctx;
  if (!strcmp(key, "tag_name")) json_str(v, r->tag, sizeof r->tag);
  else if (!strcmp(key, "html_url")) json_str(v, r->page, sizeof r->page);
  else if (!strcmp(key, "assets") && *v == '[') { // the release's files: the one of this system
    const char *p = json_ws(v + 1);
    while (*p == '{') {
      Asset a = {0};
      p = json_ws(json_members(p, asset_member, &a));
      if (UPDATE_ASSET[0] && !strcmp(a.name, UPDATE_ASSET)) { r->pick = a; r->picked = true; }
      if (*p == ',') p = json_ws(p + 1);
    }
  }
}
static int hex_digit(char c) { return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1; }

// "1.10" > "1.9": the numbers compared one by one
static int version_cmp(const char *a, const char *b) {
  while (*a || *b) {
    long x = strtol(a, (char **)&a, 10), y = strtol(b, (char **)&b, 10);
    if (x != y) return x < y ? -1 : 1;
    if (*a == '.') a++;
    if (*b == '.') b++;
    if ((*a && (*a < '0' || *a > '9')) || (*b && (*b < '0' || *b > '9'))) break;
  }
  return 0;
}

// The release's file may be installed: a name, a size, a SHA-256 from GitHub, and https (or the local
// server of the tests, when UPDATE_GITHUB_API is one)
static bool asset_ok(const Asset *a) {
  if (a->size <= 0 || a->size > (2LL << 30) || strncmp(a->digest, "sha256:", 7) || strlen(a->digest) != 7 + 64) return false;
  for (int k = 0; k < 32; k++) {
    int hi = hex_digit(a->digest[7 + 2 * k]), lo = hex_digit(a->digest[8 + 2 * k]);
    if (hi < 0 || lo < 0) return false;
    upd_asset_sha[k] = (unsigned char)(hi << 4 | lo);
  }
  bool local = !strncmp(UPDATE_GITHUB_API, "http://127.0.0.1:", 17);
  return !strncmp(a->url, "https://", 8) || (local && !strncmp(a->url, "http://127.0.0.1:", 17));
}

static DWORD WINAPI update_thread(void *arg) {
  (void)arg;
  LONG result = 4;
  if (UPDATE_GITHUB[0]) { // GitHub's newest release (not the drafts nor the pre-releases)
    int cap = 1 << 19;
    char *body = malloc((size_t)cap + 1);
    int n = body ? http_get(UPDATE_GITHUB_API "/repos/" UPDATE_GITHUB "/releases/latest", body, cap) : -1;
    if (n > 0) {
      body[n] = 0;
      static Release r;
      memset(&r, 0, sizeof r);
      json_members(body, release_member, &r);
      const char *v = r.tag[0] == 'v' || r.tag[0] == 'V' ? r.tag + 1 : r.tag;
      snprintf(upd_version, sizeof upd_version, "%s", *v >= '0' && *v <= '9' ? v : "");
      snprintf(upd_page, sizeof upd_page, "%s", !strncmp(r.page, "http", 4) ? r.page : "");
      if (upd_version[0]) result = 3; // an answer, with a version
      if (r.picked && asset_ok(&r.pick)) {
        snprintf(upd_asset_url, sizeof upd_asset_url, "%s", r.pick.url);
        upd_asset_size = r.pick.size;
      }
      if (upd_version[0] && dl_game->version && version_cmp(upd_version, dl_game->version) > 0) result = 2;
    }
    free(body);
  } else {
    char body[2048];
    int n = http_get(UPDATE_URL, body, sizeof body - 1);
    if (n > 0) {
      body[n] = 0;
      char *nl = strpbrk(body, "\r\n");
      char *page = nl ? nl + strspn(nl, "\r\n") : NULL;
      if (nl) *nl = 0;
      if (page) page[strcspn(page, "\r\n")] = 0;
      snprintf(upd_version, sizeof upd_version, "%s", body[0] >= '0' && body[0] <= '9' ? body : ""); // an error's page is no version
      snprintf(upd_page, sizeof upd_page, "%s", page && !strncmp(page, "http", 4) ? page : "");
      if (upd_version[0]) result = 3;
      if (upd_version[0] && dl_game->version && version_cmp(upd_version, dl_game->version) > 0) result = 2;
    }
  }
  InterlockedExchange(&upd_state, result);
  return 0;
}

void dl_online_start(void) {
#ifdef _WIN32
  static bool cleaned; // the installer of an update done before, left in the temporary folder
  if (!cleaned && UPDATE_ASSET[0] != 0 && upd_get == 0) {
    cleaned = true;
    char dir[MAX_PATH], file[MAX_PATH];
    GetTempPathA(MAX_PATH, dir);
    snprintf(file, sizeof file, "%s%s-update\\%s", dir, FILE_STEM, UPDATE_ASSET);
    DeleteFileA(file);
    snprintf(file, sizeof file, "%s%s-update", dir, FILE_STEM);
    RemoveDirectoryA(file);
  }
#endif
  if (!DL_UPDATE_ON || !dl_cfg.updates || upd_state == 1) return;
  InterlockedExchange(&upd_state, 1);
  HANDLE t = CreateThread(NULL, 0, update_thread, NULL, 0, NULL);
  if (t) CloseHandle(t);
  else upd_state = 4;
}

const wchar_t *dl_update_text(void) {
  static wchar_t out[96];
  if (!dl_cfg.updates) return NULL;
  if (upd_state == 2) _snwprintf(out, 95, TX(T_UPD_NEW), upd_version);
  else if (upd_state == 3 && dl_cfg.updates) lstrcpynW(out, TX(T_UPD_NONE), 96);
  else return NULL;
  out[95] = 0;
  return upd_state == 2 || dl_mode == M_PAGE ? out : NULL; // "the newest" only on the Internet page
}

void dl_update_open(void) {
  if (upd_state == 2 && upd_page[0]) ShellExecuteA(dl_hwnd, "open", upd_page, NULL, NULL, SW_SHOWNORMAL);
}

// ---- "Update": the release's file downloaded and checked on a thread, then installed
static char upd_file[MAX_PATH];
#ifdef _WIN32
static bool update_run; // Windows: the installer is started once this program has closed
#endif

typedef struct { FILE *f; Sha256 sha; long long got; } FileSink;
static bool file_sink(void *ctx, const char *d, int n) {
  FileSink *s = ctx;
  if (s->got + n > upd_asset_size || fwrite(d, 1, (size_t)n, s->f) != (size_t)n) return false; // more than GitHub said: refused
  sha_add(&s->sha, d, (size_t)n);
  s->got += n;
  InterlockedExchange(&upd_pct, (LONG)(s->got * 100 / upd_asset_size));
  return true;
}

#ifndef _WIN32
// Linux: the archive's files put in place of the program's, never over the player's own (project.h's
// personal folders and files); each file is renamed in, so that a running program is not cut short
static int run_wait(char *const argv[]) {
  pid_t pid;
  if (posix_spawnp(&pid, argv[0], NULL, NULL, argv, environ)) return -1;
  int status = 0;
  return waitpid(pid, &status, 0) == pid && WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}
static char apply_top[MAX_PATH];
static int apply_failed;
static bool personal(const char *rel) {
  static const char *const folders[] = {SETUP_PERSONAL_FOLDERS, NULL}, *const files[] = {SETUP_PERSONAL_FILES, NULL};
  size_t first = strcspn(rel, "/");
  if (!strncmp(rel, ".update", 7) && first == 7) return true;
  for (int k = 0; folders[k]; k++)
    if (strlen(folders[k]) == first && !strncmp(rel, folders[k], first)) return true;
  if (!rel[first]) // a file next to the program
    for (int k = 0; files[k]; k++)
      if (!fnmatch(files[k], rel, 0)) return true;
  return false;
}
static void apply_dir(const char *dir) { // each file under dir, put in its place beside the program
  DIR *d = opendir(dir);
  struct dirent *e;
  while (d && (e = readdir(d))) {
    if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
    char path[MAX_PATH * 2];
    snprintf(path, sizeof path, "%s/%s", dir, e->d_name);
    struct stat st;
    if (lstat(path, &st)) { apply_failed++; continue; }
    const char *rel = path + strlen(apply_top) + 1;
    if (personal(rel)) continue;
    if (S_ISDIR(st.st_mode)) { apply_dir(path); continue; }
    char to[MAX_PATH * 3];
    snprintf(to, sizeof to, "%s%s", dl_base_dir, rel);
    for (char *c = strchr(to + strlen(dl_base_dir), '/'); c; c = strchr(c + 1, '/')) { *c = 0; mkdir(to, 0755); *c = '/'; }
    if (rename(path, to)) apply_failed++;
  }
  if (d) closedir(d);
}
static bool update_apply(const char *archive) {
  char dir[MAX_PATH];
  snprintf(dir, sizeof dir, "%s.update" DL_SEP "new", dl_base_dir);
  char *rm[] = {"rm", "-rf", dir, NULL};
  run_wait(rm);
  char *mk[] = {"mkdir", "-p", dir, NULL};
  char *tar[] = {"tar", "-xzf", (char *)archive, "-C", dir, NULL};
  if (run_wait(mk) || run_wait(tar)) return false; // nothing is replaced before the archive is all out
  // the archive's one folder (Game-linux-x86_64/), or its files themselves
  snprintf(apply_top, sizeof apply_top, "%s", dir);
  DIR *d = opendir(dir);
  int entries = 0;
  char only[256] = "";
  struct dirent *e;
  while (d && (e = readdir(d)))
    if (strcmp(e->d_name, ".") && strcmp(e->d_name, "..")) { entries++; snprintf(only, sizeof only, "%s", e->d_name); }
  if (d) closedir(d);
  struct stat st;
  char inner[MAX_PATH * 2];
  snprintf(inner, sizeof inner, "%s/%s", dir, only);
  if (entries == 1 && !stat(inner, &st) && S_ISDIR(st.st_mode)) snprintf(apply_top, sizeof apply_top, "%s", inner);
  apply_failed = 0;
  apply_dir(apply_top);
  run_wait(rm);
  return !apply_failed;
}
#endif

static DWORD WINAPI install_thread(void *arg) {
  (void)arg;
#ifdef _WIN32
  char dir[MAX_PATH];
  GetTempPathA(MAX_PATH, dir);
  snprintf(upd_file, sizeof upd_file, "%s%s-update", dir, FILE_STEM);
  CreateDirectoryA(upd_file, NULL);
  snprintf(upd_file, sizeof upd_file, "%s%s-update\\%s", dir, FILE_STEM, UPDATE_ASSET);
#else
  snprintf(upd_file, sizeof upd_file, "%s.update", dl_base_dir);
  mkdir(upd_file, 0755);
  snprintf(upd_file, sizeof upd_file, "%s.update/%s", dl_base_dir, UPDATE_ASSET);
#endif
  FileSink s = {fopen(upd_file, "wb")};
  sha_init(&s.sha);
  bool ok = s.f && http_fetch(upd_asset_url, file_sink, &s, true);
  if (s.f) fclose(s.f);
  unsigned char got[32];
  sha_end(&s.sha, got);
  ok = ok && s.got == upd_asset_size && !memcmp(got, upd_asset_sha, 32); // the file GitHub described, to the byte
#ifndef _WIN32
  ok = ok && update_apply(upd_file);
  remove(upd_file); // put in place: the archive is no longer needed
#endif
  if (!ok) remove(upd_file);
  InterlockedExchange(&upd_get, ok ? 2 : 3);
  return 0;
}

void dl_update_install(void) {
  if (upd_get == 1 || upd_get == 2) return;
  if (upd_state != 2) { if (upd_state == 3) dl_note(TX(T_UPD_NONE)); return; } // no answer (a private repository...): nothing to say
  if (!upd_asset_url[0]) { dl_update_open(); return; } // no file to install: its page
  InterlockedExchange(&upd_pct, 0);
  InterlockedExchange(&upd_get, 1);
  HANDLE t = CreateThread(NULL, 0, install_thread, NULL, 0, NULL);
  if (t) CloseHandle(t);
  else upd_get = 3;
  dl_dirty = true;
}

const wchar_t *dl_update_install_show(int v) {
  (void)v;
  static wchar_t out[32];
  if (upd_state != 2) return upd_state == 3 ? TX(T_UPD_ST_NONE) : L"";
  if (!upd_asset_url[0]) return TX(T_UPD_ST_PAGE);
  if (upd_get == 1) { _snwprintf(out, 31, L"%ld %%", upd_pct); out[31] = 0; return out; }
  if (upd_get == 2) return TX(T_UPD_ST_READY);
  if (upd_get == 3) return TX(T_UPD_ST_FAIL);
  _snwprintf(out, 31, L"%hs", upd_version);
  out[31] = 0;
  return out;
}

static void update_follow(void) {
  static LONG shown_pct = -1, told;
  static DWORD ready_at;
  if (upd_get == 1 && upd_pct != shown_pct) { shown_pct = upd_pct; dl_dirty = true; }
  if (upd_get == 3 && !told) { told = 1; dl_note(TX(T_UPD_FAIL_NOTE)); dl_dirty = true; }
  if (upd_get == 2) {
    if (!ready_at) { ready_at = GetTickCount() | 1; dl_note(TX(T_UPD_RESTART)); dl_dirty = true; }
    else if (GetTickCount() - ready_at > 1500) { // the note seen, then the program closes as usual
#ifdef _WIN32
      update_run = true; // the installer, once this window is gone
      dl_running = false;
#else
      dl_relaunch(); // its new files are in place: started again
#endif
    }
  }
}

void dl_online_follow(void) {
  static LONG told;
  if (upd_state == 2 && dl_cfg.updates) dl_net_newer(); // "the new version's page" and "Update" only now
  update_follow();
  static bool test_once; // DL_TEST_UPDATE (tests): "Update" chosen by itself once a file to install is found
  if (!test_once && upd_state == 2 && upd_asset_url[0] && getenv("DL_TEST_UPDATE")) {
    test_once = true;
#ifdef _WIN32
    SetEnvironmentVariableA("DL_TEST_UPDATE", NULL); // not for the program started again afterwards
#else
    unsetenv("DL_TEST_UPDATE");
#endif
    dl_update_install();
  }
  if (upd_state == 2 && !told && dl_cfg.updates && dl_mode != M_GAME) { // once, in the menus: a note, and "Play" says it
    told = 1;
    dl_note(dl_update_text());
    dl_dirty = true;
  }
  discord_follow(false);
}

// ---------------------------------------------------------------- run-ahead
// The picture one or two frames ahead of the input (DlGame.run_ahead): the frame is played for good
// (its sound is heard), its state kept, then the frames ahead are played with the same buttons (their
// picture is shown, their sound is not), and the state put back.
static bool ahead_now;
static void *ahead_state;
static size_t ahead_cap;

bool dl_running_ahead(void) { return ahead_now; }
bool dl_ahead_ok(void) { return dl_game->run_ahead && dl_game->state_size && dl_game->state_to_mem && dl_game->state_from_mem; }

// dl_picture_same: the frame taken for good gave the picture of last time again (the frames run ahead
// do not count)
static bool pic_same;
void dl_picture_same(void) { if (!ahead_now) pic_same = true; }

static int play_frame(const DlInput *in, s16 *audio, int audio_max, int ahead) {
  pic_same = false;
  int n = dl_game->frame(in, dl_pic, audio, audio_max);
  if (ahead <= 0 || !dl_ahead_ok()) return n;
  size_t size = dl_game->state_size();
  if (!size) return n;
  if (size > ahead_cap) {
    void *more = realloc(ahead_state, size);
    if (!more) return n;
    ahead_state = more;
    ahead_cap = size;
  }
  static s16 silent[8192];
  dl_game->state_to_mem(ahead_state);
  ahead_now = true;
  for (int k = 0; k < ahead; k++) dl_game->frame(in, dl_pic, silent, audio_max < 4096 ? audio_max : 4096);
  ahead_now = false;
  dl_game->state_from_mem(ahead_state);
  return n;
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
  int fresh = 0;
  for (int k = 0; k < 90; k++) {
    pic_same = false;
    dl_game->frame(&(DlInput){k > 30 ? DL_LEFT | DL_UP : 0}, dl_pic, tmp, 4096);
    if (!pic_same) fresh++;
  }
  printf("gpu check: the game gave 90 pictures, %d new%s\n", fresh, fresh ? "" : " (it makes none: the pictures below show its last one)");
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
  if (dl_ahead_ok()) add_setting("run_ahead", &dl_cfg.ahead, 0, 2, 2);
  if (DISCORD_APP_ID[0]) add_setting("discord", &dl_cfg.discord, 0, 1, 0); // kept by "Defaults": the player's choice
  dl_cfg.updates = UPDATE_DEFAULT_ON ? 1 : 0; // its value at first, which "Defaults" keeps as the player's choice
  if (DL_UPDATE_ON) add_setting("update_check", &dl_cfg.updates, 0, 1, 0);
  add_setting("state_slot", &dl_slot, 0, DL_SLOTS - 1, 0);
  add_setting("a11y_contrast", &dl_cfg.contrast, 0, 1, 0); // kept by "Defaults": a player who needs them keeps them
  add_setting("a11y_cvd", &dl_cfg.cvd, 0, 3, 0);
  add_setting("a11y_still", &dl_cfg.still, 0, 1, 0);
  add_setting("a11y_big_help", &dl_cfg.big_help, 0, 1, 0);
  add_setting("news_seen", &dl_news_seen, 0, 0x7FFFFFFF, 0);
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
  if (crashed) { report_write(); safe_start(); }

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
  if (!bench_secs) { run_mark_write(); crash_before = SetUnhandledExceptionFilter(crash_filter); } // the marker is removed when the program closes normally
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
  u32 bench_frames = 0, bench_gaps = 0, bench_new = 0; // bench_new: the pictures that were not the last one again (dl_picture_same)
  if (!bench_secs) dl_online_start();
  if (crashed) dl_safe_page_open();
  else if (first_start && !bench_secs && !play_now) dl_welcome_open();
  else if (!bench_secs && !play_now) dl_news_open();
  else if (bench_secs || play_now) dl_play();
  LARGE_INTEGER bench_t0, menu_next = {0}; // menu_next: when the sky behind the setup screen moves next
  QueryPerformanceCounter(&bench_t0);
  while (dl_running) {
    if (bench_secs) {
      QueryPerformanceCounter(&now);
      double t = (double)(now.QuadPart - bench_t0.QuadPart) / (double)fq.QuadPart;
      if (t >= bench_secs) {
        printf("bench: %u pictures in %.1f s (%.2f per second), sound gaps %u, new pictures %u (%.2f per second)\n", bench_frames, t,
               bench_frames / t, bench_gaps, bench_new, bench_new / t);
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
    if (!bench_secs) dl_online_follow();
    { // where the player is, for a crash report (the marker), every ten seconds
      static DWORD marked;
      static int marked_mode = -1;
      if (!bench_secs && (GetTickCount() - marked > 10000 || dl_mode != marked_mode)) { // and at once when the player goes elsewhere
        marked = GetTickCount();
        marked_mode = dl_mode;
        run_mark_write();
      }
    }
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
    bool turbo = dl_mode == M_GAME && !(game->hide & DL_HIDE_TURBO) && (dl_turbo_held() || (dl_started && game->turbo && game->turbo()));
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
        // run-ahead only in play, at the normal speed
        int n = play_frame(&in, audio, audio_max, dl_mode == M_GAME && dl_started && speed == 1 ? dl_cfg.ahead : 0);
        if (n > 0 && audio && !turbo) audio_add(audio, n);
        if (dl_mode == M_GAME && dl_started) dl_rewind_snap();
        fps_frames++;
        bench_frames++;
        if (!pic_same) bench_new++;
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
#ifdef _WIN32
  if (update_run) { // the release's installer, silent, over this folder: it waits for this program to end, then starts it again
    DestroyWindow(dl_hwnd);
    wchar_t file[MAX_PATH], dir[MAX_PATH], cmd[3 * MAX_PATH];
    MultiByteToWideChar(CP_ACP, 0, upd_file, -1, file, MAX_PATH);
    MultiByteToWideChar(CP_ACP, 0, dl_base_dir, -1, dir, MAX_PATH);
    size_t dn = wcslen(dir);
    while (dn > 3 && dir[dn - 1] == L'\\') dir[--dn] = 0;
    _snwprintf(cmd, 3 * MAX_PATH - 1, L"\"%ls\" /S \"/D=%ls\" /NOSHORTCUTS /WAIT=%lu /RUN", file, dir, (unsigned long)GetCurrentProcessId());
    cmd[3 * MAX_PATH - 1] = 0;
    STARTUPINFOW si = {sizeof si};
    PROCESS_INFORMATION pi;
    if (CreateProcessW(file, cmd, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
      CloseHandle(pi.hThread);
      CloseHandle(pi.hProcess);
    }
    return 0;
  }
#endif
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
