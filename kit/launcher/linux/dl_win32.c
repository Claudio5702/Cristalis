// The rest of what DarkLauncher's shared code takes from Windows (linux/win32.h), for Linux, made with SDL3
// and POSIX: the window and its messages (SDL's events turned into WM_*), the window's picture (what GDI
// drew into it, shown through an SDL texture when the DC is given back), the sound (waveOut and
// PlaySound as SDL audio streams), the keyboard (Windows' virtual-key codes, laid out as the system's
// keyboard), files and folders, the trash, dates, the process, and the pictures GDI+ reads (PNG, BMP).
#include "win32.h"
#include <SDL3/SDL.h>
#include <ctype.h>
#include <dirent.h>
#include <limits.h>
#include <errno.h>
#include <fnmatch.h>
#include <signal.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include <locale.h>

// Wide characters as UTF-8 for printf's %ls, towupper and the rest: the system's language if it is UTF-8,
// C.UTF-8 otherwise. Before anything else runs.
__attribute__((constructor)) static void utf8_locale(void) {
  const char *l = setlocale(LC_CTYPE, "");
  if (!l || (!strstr(l, "UTF-8") && !strstr(l, "utf8"))) setlocale(LC_CTYPE, "C.UTF-8");
}

void dl_pads_added(SDL_JoystickID id); // dl_pads.c
void dl_pads_removed(SDL_JoystickID id);

static bool sdl_up(void) {
  static int state = -1;
  if (state < 0) state = SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMEPAD);
  return state;
}

// ---- UTF-8 and wide strings (the system's text is UTF-8; CP_ACP is taken as UTF-8 too)
static int utf8_to_wide(const char *s, int n, wchar_t *out, int cap) {
  int k = 0;
  const unsigned char *p = (const unsigned char *)s, *e = n < 0 ? NULL : p + n;
  while ((e ? p < e : 1)) {
    unsigned c = *p;
    if (!e && !c) break;
    int len = c < 0x80 ? 1 : c < 0xE0 ? 2 : c < 0xF0 ? 3 : 4;
    unsigned v = len == 1 ? c : len == 2 ? c & 0x1F : len == 3 ? c & 0x0F : c & 0x07;
    for (int i = 1; i < len; i++) v = v << 6 | (p[i] & 0x3F);
    p += len;
    if (out && k < cap) out[k] = (wchar_t)v;
    k++;
  }
  if (n < 0) { // with the ending zero, as Windows counts it
    if (out && k < cap) out[k] = 0;
    else if (out && cap > 0) out[cap - 1] = 0;
    k++;
  }
  return k;
}

static int wide_to_utf8(const wchar_t *s, int n, char *out, int cap) {
  int k = 0;
  for (int i = 0; n < 0 ? s[i] : i < n; i++) {
    unsigned v = (unsigned)s[i];
    char b[4];
    int len = v < 0x80 ? (b[0] = (char)v, 1) : v < 0x800 ? (b[0] = (char)(0xC0 | v >> 6), b[1] = (char)(0x80 | (v & 0x3F)), 2)
            : v < 0x10000 ? (b[0] = (char)(0xE0 | v >> 12), b[1] = (char)(0x80 | (v >> 6 & 0x3F)), b[2] = (char)(0x80 | (v & 0x3F)), 3)
                          : (b[0] = (char)(0xF0 | v >> 18), b[1] = (char)(0x80 | (v >> 12 & 0x3F)), b[2] = (char)(0x80 | (v >> 6 & 0x3F)), b[3] = (char)(0x80 | (v & 0x3F)), 4);
    for (int j = 0; j < len; j++, k++)
      if (out && k < cap) out[k] = b[j];
  }
  if (n < 0) {
    if (out && k < cap) out[k] = 0;
    else if (out && cap > 0) out[cap - 1] = 0;
    k++;
  }
  return k;
}

int MultiByteToWideChar(UINT cp, DWORD flags, const char *s, int n, wchar_t *out, int cap) {
  (void)cp, (void)flags;
  return utf8_to_wide(s, n, out, cap);
}
int WideCharToMultiByte(UINT cp, DWORD flags, const wchar_t *s, int n, char *out, int cap, const char *def, BOOL *used) {
  (void)cp, (void)flags, (void)def;
  if (used) *used = FALSE;
  return wide_to_utf8(s, n, out, cap);
}

// ---- time
BOOL QueryPerformanceCounter(LARGE_INTEGER *c) { c->QuadPart = (LONGLONG)SDL_GetPerformanceCounter(); return TRUE; }
BOOL QueryPerformanceFrequency(LARGE_INTEGER *f) { f->QuadPart = (LONGLONG)SDL_GetPerformanceFrequency(); return TRUE; }
DWORD GetTickCount(void) { return (DWORD)SDL_GetTicks(); }
void Sleep(DWORD ms) { SDL_Delay(ms); }
UINT timeBeginPeriod(UINT ms) { (void)ms; return 0; }
UINT timeEndPeriod(UINT ms) { (void)ms; return 0; }
long DwmFlush(void) { return 0; }

static FILETIME ft_of(time_t t, long ns) {
  unsigned long long v = ((unsigned long long)t + 11644473600ull) * 10000000ull + (unsigned long long)ns / 100;
  return (FILETIME){(DWORD)v, (DWORD)(v >> 32)};
}
static unsigned long long ft_value(const FILETIME *f) { return (unsigned long long)f->dwHighDateTime << 32 | f->dwLowDateTime; }

BOOL FileTimeToLocalFileTime(const FILETIME *ft, FILETIME *local) {
  time_t t = (time_t)(ft_value(ft) / 10000000ull - 11644473600ull);
  struct tm tm;
  localtime_r(&t, &tm);
  unsigned long long v = ft_value(ft) + (unsigned long long)((long long)tm.tm_gmtoff * 10000000ll);
  *local = (FILETIME){(DWORD)v, (DWORD)(v >> 32)};
  return TRUE;
}

static void st_of(const struct tm *tm, int ms, SYSTEMTIME *st) {
  *st = (SYSTEMTIME){(WORD)(tm->tm_year + 1900), (WORD)(tm->tm_mon + 1), (WORD)tm->tm_wday, (WORD)tm->tm_mday, (WORD)tm->tm_hour, (WORD)tm->tm_min, (WORD)tm->tm_sec, (WORD)ms};
}

BOOL FileTimeToSystemTime(const FILETIME *ft, SYSTEMTIME *st) {
  unsigned long long v = ft_value(ft);
  time_t t = (time_t)(v / 10000000ull - 11644473600ull);
  struct tm tm;
  gmtime_r(&t, &tm);
  st_of(&tm, (int)(v / 10000 % 1000), st);
  return TRUE;
}

LONG CompareFileTime(const FILETIME *a, const FILETIME *b) {
  unsigned long long x = ft_value(a), y = ft_value(b);
  return x < y ? -1 : x > y ? 1 : 0;
}

void GetLocalTime(SYSTEMTIME *st) {
  struct timespec ts;
  clock_gettime(CLOCK_REALTIME, &ts);
  struct tm tm;
  localtime_r(&ts.tv_sec, &tm);
  st_of(&tm, (int)(ts.tv_nsec / 1000000), st);
}

// Dates as the system's language writes them: year first in Japanese, month first in American English.
static const char *lang_env(void) {
  const char *e = getenv("LC_ALL");
  if (!e || !*e) e = getenv("LC_TIME");
  if (!e || !*e) e = getenv("LANG");
  return e ? e : "";
}

int GetDateFormatW(DWORD locale, DWORD flags, const SYSTEMTIME *st, const wchar_t *fmt, wchar_t *out, int n) {
  (void)locale, (void)flags, (void)fmt;
  const char *l = lang_env();
  if (!strncmp(l, "ja", 2)) return swprintf(out, (size_t)n, L"%04d/%02d/%02d", st->wYear, st->wMonth, st->wDay) + 1;
  if (!strncmp(l, "en_US", 5)) return swprintf(out, (size_t)n, L"%02d/%02d/%04d", st->wMonth, st->wDay, st->wYear) + 1;
  return swprintf(out, (size_t)n, L"%02d/%02d/%04d", st->wDay, st->wMonth, st->wYear) + 1;
}

int GetTimeFormatW(DWORD locale, DWORD flags, const SYSTEMTIME *st, const wchar_t *fmt, wchar_t *out, int n) {
  (void)locale, (void)fmt;
  if (flags & TIME_NOSECONDS) return swprintf(out, (size_t)n, L"%02d:%02d", st->wHour, st->wMinute) + 1;
  return swprintf(out, (size_t)n, L"%02d:%02d:%02d", st->wHour, st->wMinute, st->wSecond) + 1;
}

WORD GetUserDefaultUILanguage(void) {
  static const struct { const char *code; WORD id; } codes[] = {{"ja", 0x11}, {"fr", 0x0C}, {"es", 0x0A}, {"pt", 0x16}, {"de", 0x07}, {"it", 0x10}};
  const char *e = getenv("LC_ALL");
  if (!e || !*e) e = getenv("LANG");
  for (size_t k = 0; e && k < sizeof codes / sizeof codes[0]; k++)
    if (!strncmp(e, codes[k].code, 2)) return codes[k].id;
  return 0x09; // English
}

// ---- handles: a folder being read, a process
enum { H_FIND = 1, H_PROC };
typedef struct {
  int kind;
  DIR *dir;
  char path[MAX_PATH], pattern[MAX_PATH];
  pid_t pid;
} Handle;

BOOL CloseHandle(HANDLE h) {
  Handle *x = h;
  if (!h || h == INVALID_HANDLE_VALUE || (uintptr_t)h < 16) return FALSE;
  if (x->kind == H_FIND && x->dir) closedir(x->dir);
  free(x);
  return TRUE;
}

// ---- the process
DWORD GetCurrentProcessId(void) { return (DWORD)getpid(); }

HANDLE OpenProcess(DWORD access, BOOL inherit, DWORD pid) {
  (void)access, (void)inherit;
  if (kill((pid_t)pid, 0) && errno != EPERM) return NULL;
  Handle *h = calloc(1, sizeof *h);
  h->kind = H_PROC;
  h->pid = (pid_t)pid;
  return h;
}

BOOL GetExitCodeProcess(HANDLE h, DWORD *code) {
  Handle *x = h;
  *code = kill(x->pid, 0) == 0 || errno == EPERM ? STILL_ACTIVE : 0;
  return TRUE;
}

static bool exe_of(pid_t pid, char *out, size_t n) {
  char link[64];
  snprintf(link, sizeof link, "/proc/%d/exe", (int)pid);
  ssize_t k = readlink(link, out, n - 1);
  if (k <= 0) return false;
  out[k] = 0;
  return true;
}

BOOL QueryFullProcessImageNameW(HANDLE h, DWORD flags, wchar_t *out, DWORD *n) {
  (void)flags;
  char p[MAX_PATH];
  if (!exe_of(((Handle *)h)->pid, p, sizeof p)) return FALSE;
  *n = (DWORD)utf8_to_wide(p, -1, out, (int)*n) - 1;
  return TRUE;
}

DWORD GetModuleFileNameA(HMODULE m, char *out, DWORD n) {
  (void)m;
  if (!exe_of(getpid(), out, n)) { out[0] = 0; return 0; }
  return (DWORD)strlen(out);
}

DWORD GetModuleFileNameW(HMODULE m, wchar_t *out, DWORD n) {
  char p[MAX_PATH];
  GetModuleFileNameA(m, p, sizeof p);
  return (DWORD)utf8_to_wide(p, -1, out, (int)n) - 1;
}

HMODULE GetModuleHandleW(LPCWSTR name) { (void)name; return NULL; }
const wchar_t *GetCommandLineW(void) { return L""; }

// The same program again, with the same arguments (/proc/self/cmdline), once this one is gone.
BOOL CreateProcessW(const wchar_t *exe, const wchar_t *cmd, void *pa, void *ta, BOOL inherit, DWORD flags, void *env, const wchar_t *dir,
                    STARTUPINFOW *si, PROCESS_INFORMATION *pi) {
  (void)exe, (void)cmd, (void)pa, (void)ta, (void)inherit, (void)flags, (void)env, (void)dir, (void)si;
  static char buf[8192];
  FILE *f = fopen("/proc/self/cmdline", "rb");
  if (!f) return FALSE;
  size_t n = fread(buf, 1, sizeof buf - 1, f);
  fclose(f);
  buf[n] = 0;
  char *argv[128];
  int argc = 0;
  for (size_t k = 0; k < n && argc < 127; k += strlen(buf + k) + 1) argv[argc++] = buf + k;
  argv[argc] = NULL;
  pid_t pid = fork();
  if (pid < 0) return FALSE;
  if (!pid) {
    setsid();
    execv("/proc/self/exe", argv);
    _exit(127);
  }
  memset(pi, 0, sizeof *pi);
  pi->dwProcessId = (DWORD)pid;
  return TRUE;
}

HANDLE GetStdHandle(DWORD which) { (void)which; return (HANDLE)1; }
DWORD GetFileType(HANDLE h) { (void)h; return FILE_TYPE_CHAR; } // the terminal's output is already there
BOOL AttachConsole(DWORD pid) { (void)pid; return FALSE; }
void SetProcessDPIAware(void) {}

// ---- libraries: XInput (dl_pads.c) and GDI+'s picture reading (PNG and BMP through SDL)
static int WINAPI gp_startup(ULONG_PTR *token, const void *in, void *out) { (void)in, (void)out; *token = 1; return 0; }

static int WINAPI gp_from_file(const wchar_t *path, void **img) {
  char p[MAX_PATH * 3];
  wide_to_utf8(path, -1, p, sizeof p);
  const char *dot = strrchr(p, '.');
  SDL_Surface *s = !dot ? NULL : !strcasecmp(dot, ".png") ? SDL_LoadPNG(p) : !strcasecmp(dot, ".bmp") ? SDL_LoadBMP(p) : NULL;
  if (!s) return 2;
  *img = SDL_ConvertSurface(s, SDL_PIXELFORMAT_ARGB8888);
  SDL_DestroySurface(s);
  return *img ? 0 : 2;
}

static int WINAPI gp_to_hbitmap(void *img, HBITMAP *out, DWORD bg) {
  SDL_Surface *s = img;
  BITMAPINFO bi = {{sizeof(BITMAPINFOHEADER), s->w, -s->h, 1, 32}};
  uint32_t *bits = NULL;
  *out = CreateDIBSection(NULL, &bi, DIB_RGB_COLORS, (void **)&bits, NULL, 0);
  for (int y = 0; y < s->h; y++) {
    const uint32_t *row = (const uint32_t *)((const uint8_t *)s->pixels + (size_t)y * s->pitch);
    for (int x = 0; x < s->w; x++) { // over the background colour, as GDI+ does
      uint32_t c = row[x], a = c >> 24, o = 0;
      for (int k = 0; k < 24; k += 8) o |= (((c >> k & 255) * a + (bg >> k & 255) * (255 - a)) / 255) << k;
      bits[(size_t)y * s->w + x] = o;
    }
  }
  return 0;
}

static int WINAPI gp_dispose(void *img) { SDL_DestroySurface(img); return 0; }

HMODULE LoadLibraryA(const char *name) {
  if (strstr(name, "xinput")) return (HMODULE)1;
  if (strstr(name, "gdiplus")) return (HMODULE)2;
  return NULL; // hid.dll, setupapi.dll, d3dcompiler: not on Linux (SDL reads PlayStation pads; no graphics card yet)
}

void *GetProcAddress(HMODULE m, const char *name) {
  if (m == (HMODULE)1) return !strcmp(name, "XInputGetState") ? (void *)dl_xinput_get : !strcmp(name, "XInputSetState") ? (void *)dl_xinput_set : NULL;
  if (m == (HMODULE)2) {
    if (!strcmp(name, "GdiplusStartup")) return (void *)gp_startup;
    if (!strcmp(name, "GdipCreateBitmapFromFile")) return (void *)gp_from_file;
    if (!strcmp(name, "GdipCreateHBITMAPFromBitmap")) return (void *)gp_to_hbitmap;
    if (!strcmp(name, "GdipDisposeImage")) return (void *)gp_dispose;
  }
  return NULL;
}

// ---- files and folders
static void attrs_of(const struct stat *st, DWORD *attr, FILETIME *c, FILETIME *a, FILETIME *w, DWORD *hi, DWORD *lo) {
  *attr = S_ISDIR(st->st_mode) ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_NORMAL;
  *c = ft_of(st->st_ctim.tv_sec, st->st_ctim.tv_nsec);
  *a = ft_of(st->st_atim.tv_sec, st->st_atim.tv_nsec);
  *w = ft_of(st->st_mtim.tv_sec, st->st_mtim.tv_nsec);
  *hi = (DWORD)((unsigned long long)st->st_size >> 32);
  *lo = (DWORD)st->st_size;
}

DWORD GetFileAttributesA(const char *path) {
  struct stat st;
  if (stat(path, &st)) return INVALID_FILE_ATTRIBUTES;
  return S_ISDIR(st.st_mode) ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_NORMAL;
}

BOOL GetFileAttributesExA(const char *path, int level, void *out) {
  (void)level;
  struct stat st;
  if (stat(path, &st)) return FALSE;
  WIN32_FILE_ATTRIBUTE_DATA *d = out;
  attrs_of(&st, &d->dwFileAttributes, &d->ftCreationTime, &d->ftLastAccessTime, &d->ftLastWriteTime, &d->nFileSizeHigh, &d->nFileSizeLow);
  return TRUE;
}

// The next entry of the folder that matches the pattern, with its attributes.
static bool find_next(Handle *h, WIN32_FIND_DATAA *fd) {
  struct dirent *e;
  while ((e = readdir(h->dir))) {
    if (fnmatch(h->pattern, e->d_name, 0)) continue;
    char full[MAX_PATH * 2];
    snprintf(full, sizeof full, "%s/%s", h->path, e->d_name);
    struct stat st;
    memset(fd, 0, sizeof *fd);
    if (!stat(full, &st)) attrs_of(&st, &fd->dwFileAttributes, &fd->ftCreationTime, &fd->ftLastAccessTime, &fd->ftLastWriteTime, &fd->nFileSizeHigh, &fd->nFileSizeLow);
    snprintf(fd->cFileName, sizeof fd->cFileName, "%s", e->d_name);
    return true;
  }
  return false;
}

HANDLE FindFirstFileA(const char *pattern, WIN32_FIND_DATAA *fd) {
  Handle *h = calloc(1, sizeof *h);
  h->kind = H_FIND;
  const char *slash = strrchr(pattern, '/');
  if (slash) {
    snprintf(h->path, sizeof h->path, "%.*s", (int)(slash - pattern), pattern);
    snprintf(h->pattern, sizeof h->pattern, "%s", slash + 1);
  } else {
    snprintf(h->path, sizeof h->path, ".");
    snprintf(h->pattern, sizeof h->pattern, "%s", pattern);
  }
  h->dir = opendir(h->path[0] ? h->path : "/");
  if (!h->dir || !find_next(h, fd)) { CloseHandle(h); return INVALID_HANDLE_VALUE; }
  return h;
}

BOOL FindNextFileA(HANDLE h, WIN32_FIND_DATAA *fd) { return find_next(h, fd); }

static void wide_fd(const WIN32_FIND_DATAA *a, WIN32_FIND_DATAW *w) {
  memcpy(w, a, offsetof(WIN32_FIND_DATAA, cFileName)); // the same fields before the names
  utf8_to_wide(a->cFileName, -1, w->cFileName, MAX_PATH);
  w->cAlternateFileName[0] = 0;
}

HANDLE FindFirstFileW(const wchar_t *pattern, WIN32_FIND_DATAW *fd) {
  char p[MAX_PATH * 3];
  WIN32_FIND_DATAA a;
  wide_to_utf8(pattern, -1, p, sizeof p);
  HANDLE h = FindFirstFileA(p, &a);
  if (h != INVALID_HANDLE_VALUE) wide_fd(&a, fd);
  return h;
}

BOOL FindNextFileW(HANDLE h, WIN32_FIND_DATAW *fd) {
  WIN32_FIND_DATAA a;
  if (!find_next(h, &a)) return FALSE;
  wide_fd(&a, fd);
  return TRUE;
}

BOOL FindClose(HANDLE h) { return CloseHandle(h); }
BOOL CreateDirectoryA(const char *path, void *sa) { (void)sa; return mkdir(path, 0755) == 0; }
BOOL CreateDirectoryW(const wchar_t *path, void *sa) {
  char p[MAX_PATH * 3];
  wide_to_utf8(path, -1, p, sizeof p);
  return CreateDirectoryA(p, sa);
}
BOOL DeleteFileA(const char *path) { return unlink(path) == 0; }

// A file into the desktop's trash (freedesktop.org: Trash/files and its .trashinfo), as the recycle bin.
static bool trash(const char *path) {
  char full[PATH_MAX], base[MAX_PATH * 2], dir[MAX_PATH * 2], name[MAX_PATH], info[MAX_PATH * 3], to[MAX_PATH * 3];
  if (!realpath(path, full)) return false;
  const char *data = getenv("XDG_DATA_HOME"), *home = getenv("HOME");
  if (data && *data) snprintf(base, sizeof base, "%s/Trash", data);
  else if (home) snprintf(base, sizeof base, "%s/.local/share/Trash", home);
  else return false;
  mkdir(base, 0700);
  snprintf(dir, sizeof dir, "%s/files", base);
  mkdir(dir, 0700);
  snprintf(dir, sizeof dir, "%s/info", base);
  mkdir(dir, 0700);
  const char *file = strrchr(full, '/') ? strrchr(full, '/') + 1 : full;
  for (int k = 0; k < 1000; k++) { // a name not used yet in the trash
    if (k) snprintf(name, sizeof name, "%s.%d", file, k);
    else snprintf(name, sizeof name, "%s", file);
    snprintf(info, sizeof info, "%s/info/%s.trashinfo", base, name);
    struct stat st;
    if (stat(info, &st)) break;
  }
  FILE *f = fopen(info, "w");
  if (!f) return false;
  time_t now = time(NULL);
  struct tm tm;
  localtime_r(&now, &tm);
  fprintf(f, "[Trash Info]\nPath=");
  for (const unsigned char *p = (const unsigned char *)full; *p; p++) // percent-encoded, as the specification wants
    if (isalnum(*p) || strchr("/-_.~", *p)) fputc(*p, f);
    else fprintf(f, "%%%02X", *p);
  fprintf(f, "\nDeletionDate=%04d-%02d-%02dT%02d:%02d:%02d\n", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
  fclose(f);
  snprintf(to, sizeof to, "%s/files/%s", base, name);
  if (rename(full, to)) { unlink(info); return false; }
  return true;
}

int SHFileOperationA(SHFILEOPSTRUCTA *op) {
  if (op->wFunc != FO_DELETE) return 1;
  for (const char *p = op->pFrom; *p; p += strlen(p) + 1)
    if (!(op->fFlags & FOF_ALLOWUNDO) || !trash(p)) unlink(p); // no trash on another disk: deleted
  return 0;
}

// Opens a folder or a file with the desktop's own program (xdg-open), not waiting for it.
static HINSTANCE open_with_desktop(const char *path) {
  pid_t pid = fork();
  if (!pid) {
    setsid();
    execlp("xdg-open", "xdg-open", path, (char *)NULL);
    _exit(127);
  }
  return (HINSTANCE)(uintptr_t)(pid > 0 ? 33 : 2); // above 32: done, as ShellExecute says
}

HINSTANCE ShellExecuteA(HWND w, const char *op, const char *file, const char *params, const char *dir, int show) {
  (void)w, (void)op, (void)params, (void)dir, (void)show;
  return open_with_desktop(file);
}

HINSTANCE ShellExecuteW(HWND w, const wchar_t *op, const wchar_t *file, const wchar_t *params, const wchar_t *dir, int show) {
  (void)w, (void)op, (void)params, (void)dir, (void)show;
  char p[MAX_PATH * 3];
  wide_to_utf8(file, -1, p, sizeof p);
  return open_with_desktop(p);
}

// ---- the keyboard: one table between Windows' virtual-key codes, PC scan codes and SDL's
typedef struct { SDL_Scancode sc; int vk, set1; bool ext; } Key;
static const Key keys[] = {
    {SDL_SCANCODE_A, 'A', 0x1E}, {SDL_SCANCODE_B, 'B', 0x30}, {SDL_SCANCODE_C, 'C', 0x2E}, {SDL_SCANCODE_D, 'D', 0x20}, {SDL_SCANCODE_E, 'E', 0x12},
    {SDL_SCANCODE_F, 'F', 0x21}, {SDL_SCANCODE_G, 'G', 0x22}, {SDL_SCANCODE_H, 'H', 0x23}, {SDL_SCANCODE_I, 'I', 0x17}, {SDL_SCANCODE_J, 'J', 0x24},
    {SDL_SCANCODE_K, 'K', 0x25}, {SDL_SCANCODE_L, 'L', 0x26}, {SDL_SCANCODE_M, 'M', 0x32}, {SDL_SCANCODE_N, 'N', 0x31}, {SDL_SCANCODE_O, 'O', 0x18},
    {SDL_SCANCODE_P, 'P', 0x19}, {SDL_SCANCODE_Q, 'Q', 0x10}, {SDL_SCANCODE_R, 'R', 0x13}, {SDL_SCANCODE_S, 'S', 0x1F}, {SDL_SCANCODE_T, 'T', 0x14},
    {SDL_SCANCODE_U, 'U', 0x16}, {SDL_SCANCODE_V, 'V', 0x2F}, {SDL_SCANCODE_W, 'W', 0x11}, {SDL_SCANCODE_X, 'X', 0x2D}, {SDL_SCANCODE_Y, 'Y', 0x15},
    {SDL_SCANCODE_Z, 'Z', 0x2C}, {SDL_SCANCODE_1, '1', 0x02}, {SDL_SCANCODE_2, '2', 0x03}, {SDL_SCANCODE_3, '3', 0x04}, {SDL_SCANCODE_4, '4', 0x05},
    {SDL_SCANCODE_5, '5', 0x06}, {SDL_SCANCODE_6, '6', 0x07}, {SDL_SCANCODE_7, '7', 0x08}, {SDL_SCANCODE_8, '8', 0x09}, {SDL_SCANCODE_9, '9', 0x0A},
    {SDL_SCANCODE_0, '0', 0x0B}, {SDL_SCANCODE_RETURN, VK_RETURN, 0x1C}, {SDL_SCANCODE_ESCAPE, VK_ESCAPE, 0x01}, {SDL_SCANCODE_BACKSPACE, VK_BACK, 0x0E},
    {SDL_SCANCODE_TAB, VK_TAB, 0x0F}, {SDL_SCANCODE_SPACE, VK_SPACE, 0x39}, {SDL_SCANCODE_MINUS, 0xBD, 0x0C}, {SDL_SCANCODE_EQUALS, 0xBB, 0x0D},
    {SDL_SCANCODE_LEFTBRACKET, 0xDB, 0x1A}, {SDL_SCANCODE_RIGHTBRACKET, 0xDD, 0x1B}, {SDL_SCANCODE_BACKSLASH, 0xDC, 0x2B}, {SDL_SCANCODE_SEMICOLON, 0xBA, 0x27},
    {SDL_SCANCODE_APOSTROPHE, 0xDE, 0x28}, {SDL_SCANCODE_GRAVE, 0xC0, 0x29}, {SDL_SCANCODE_COMMA, 0xBC, 0x33}, {SDL_SCANCODE_PERIOD, 0xBE, 0x34},
    {SDL_SCANCODE_SLASH, 0xBF, 0x35}, {SDL_SCANCODE_NONUSBACKSLASH, 0xE2, 0x56}, {SDL_SCANCODE_CAPSLOCK, 0x14, 0x3A},
    {SDL_SCANCODE_F1, VK_F1, 0x3B}, {SDL_SCANCODE_F2, 0x71, 0x3C}, {SDL_SCANCODE_F3, 0x72, 0x3D}, {SDL_SCANCODE_F4, 0x73, 0x3E}, {SDL_SCANCODE_F5, 0x74, 0x3F},
    {SDL_SCANCODE_F6, 0x75, 0x40}, {SDL_SCANCODE_F7, 0x76, 0x41}, {SDL_SCANCODE_F8, 0x77, 0x42}, {SDL_SCANCODE_F9, 0x78, 0x43}, {SDL_SCANCODE_F10, 0x79, 0x44},
    {SDL_SCANCODE_F11, 0x7A, 0x57}, {SDL_SCANCODE_F12, 0x7B, 0x58}, {SDL_SCANCODE_LSHIFT, VK_LSHIFT, 0x2A}, {SDL_SCANCODE_RSHIFT, VK_RSHIFT, 0x36},
    {SDL_SCANCODE_LCTRL, VK_LCONTROL, 0x1D}, {SDL_SCANCODE_RCTRL, VK_RCONTROL, 0x1D, true}, {SDL_SCANCODE_LALT, VK_LMENU, 0x38}, {SDL_SCANCODE_RALT, VK_RMENU, 0x38, true},
    {SDL_SCANCODE_INSERT, VK_INSERT, 0x52, true}, {SDL_SCANCODE_DELETE, VK_DELETE, 0x53, true}, {SDL_SCANCODE_HOME, VK_HOME, 0x47, true},
    {SDL_SCANCODE_END, VK_END, 0x4F, true}, {SDL_SCANCODE_PAGEUP, VK_PRIOR, 0x49, true}, {SDL_SCANCODE_PAGEDOWN, VK_NEXT, 0x51, true},
    {SDL_SCANCODE_RIGHT, VK_RIGHT, 0x4D, true}, {SDL_SCANCODE_LEFT, VK_LEFT, 0x4B, true}, {SDL_SCANCODE_DOWN, VK_DOWN, 0x50, true}, {SDL_SCANCODE_UP, VK_UP, 0x48, true},
    {SDL_SCANCODE_KP_DIVIDE, VK_DIVIDE, 0x35, true}, {SDL_SCANCODE_KP_MULTIPLY, 0x6A, 0x37}, {SDL_SCANCODE_KP_MINUS, 0x6D, 0x4A}, {SDL_SCANCODE_KP_PLUS, 0x6B, 0x4E},
    {SDL_SCANCODE_KP_ENTER, VK_RETURN, 0x1C, true}, {SDL_SCANCODE_KP_1, 0x61, 0x4F}, {SDL_SCANCODE_KP_2, 0x62, 0x50}, {SDL_SCANCODE_KP_3, 0x63, 0x51},
    {SDL_SCANCODE_KP_4, 0x64, 0x4B}, {SDL_SCANCODE_KP_5, 0x65, 0x4C}, {SDL_SCANCODE_KP_6, 0x66, 0x4D}, {SDL_SCANCODE_KP_7, 0x67, 0x47}, {SDL_SCANCODE_KP_8, 0x68, 0x48},
    {SDL_SCANCODE_KP_9, 0x69, 0x49}, {SDL_SCANCODE_KP_0, 0x60, 0x52}, {SDL_SCANCODE_KP_PERIOD, 0x6E, 0x53}, {SDL_SCANCODE_NUMLOCKCLEAR, 0x90, 0x45},
    {SDL_SCANCODE_SCROLLLOCK, 0x91, 0x46},
};
#define KEYS ((int)(sizeof keys / sizeof keys[0]))

// A letter or digit key's code follows the keyboard's layout (on AZERTY, the key left of Z is A), as on Windows.
static int vk_of(SDL_Scancode sc) {
  for (int k = 0; k < KEYS; k++)
    if (keys[k].sc == sc) {
      if ((keys[k].vk >= 'A' && keys[k].vk <= 'Z') || (keys[k].vk >= '0' && keys[k].vk <= '9')) {
        SDL_Keycode c = SDL_GetKeyFromScancode(sc, SDL_KMOD_NONE, false);
        if (c >= 'a' && c <= 'z') return (int)(c - 'a' + 'A');
        if (c >= '0' && c <= '9') return (int)c;
      }
      return keys[k].vk;
    }
  return 0;
}

static SDL_Scancode sc_of(int vk) {
  if ((vk >= 'A' && vk <= 'Z') || (vk >= '0' && vk <= '9')) {
    SDL_Scancode s = SDL_GetScancodeFromKey((SDL_Keycode)(vk >= 'A' && vk <= 'Z' ? vk - 'A' + 'a' : vk), NULL);
    if (s != SDL_SCANCODE_UNKNOWN) return s;
  }
  if (vk == VK_SHIFT) return SDL_SCANCODE_LSHIFT;
  for (int k = 0; k < KEYS; k++)
    if (keys[k].vk == vk) return keys[k].sc;
  return SDL_SCANCODE_UNKNOWN;
}

static const Key *key_of_sc(SDL_Scancode sc) {
  for (int k = 0; k < KEYS; k++)
    if (keys[k].sc == sc) return &keys[k];
  return NULL;
}

SHORT GetAsyncKeyState(int vk) {
  if (!sdl_up()) return 0;
  const bool *state = SDL_GetKeyboardState(NULL);
  SDL_Scancode s = sc_of(vk);
  if (vk == VK_SHIFT) return (SHORT)(state[SDL_SCANCODE_LSHIFT] || state[SDL_SCANCODE_RSHIFT] ? 0x8000 : 0);
  return (SHORT)(s != SDL_SCANCODE_UNKNOWN && state[s] ? 0x8000 : 0);
}

UINT MapVirtualKeyW(UINT code, UINT type) {
  if (type == MAPVK_VSC_TO_VK) {
    for (int k = 0; k < KEYS; k++)
      if (keys[k].set1 == (int)code && !keys[k].ext) return (UINT)vk_of(keys[k].sc);
    return 0;
  }
  SDL_Scancode s = sc_of((int)code);
  const Key *k = key_of_sc(s);
  return k ? (UINT)k->set1 : 0;
}

int GetKeyNameTextW(LONG lp, wchar_t *out, int n) {
  int set1 = lp >> 16 & 0xFF;
  bool ext = lp >> 24 & 1;
  for (int k = 0; k < KEYS; k++)
    if (keys[k].set1 == set1 && keys[k].ext == ext) {
      SDL_Keycode c = SDL_GetKeyFromScancode(keys[k].sc, SDL_KMOD_NONE, false);
      const char *name = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ? SDL_GetKeyName(c) : SDL_GetScancodeName(keys[k].sc);
      if (!name || !*name) return 0;
      return utf8_to_wide(name, -1, out, n) - 1;
    }
  return 0;
}

// ---- controllers by WinMM: none (SDL reads them all, dl_pads.c)
UINT joyGetNumDevs(void) { return 0; }
UINT joyGetPosEx(UINT id, JOYINFOEX *ji) { (void)id, (void)ji; return 167; } // JOYERR_UNPLUGGED
UINT joyGetDevCapsW(UINT id, JOYCAPSW *caps, UINT size) { (void)id, (void)caps, (void)size; return 165; }

// ---- sound: waveOut's buffers into an SDL audio stream, each marked done once played
#define WAVE_MAX 32
typedef struct {
  SDL_AudioStream *s;
  WAVEHDR *pend[WAVE_MAX];
  long long end[WAVE_MAX], total;
  int n, frame_bytes;
} Wave;
static Wave *waves[4];

static void wave_follow(Wave *w) {
  long long played = w->total - SDL_GetAudioStreamQueued(w->s);
  int k = 0;
  while (k < w->n && w->end[k] <= played) w->pend[k++]->dwFlags |= WHDR_DONE;
  if (k) {
    memmove(w->pend, w->pend + k, sizeof w->pend[0] * (size_t)(w->n - k));
    memmove(w->end, w->end + k, sizeof w->end[0] * (size_t)(w->n - k));
    w->n -= k;
  }
}

static void audio_follow(void) {
  for (int k = 0; k < 4; k++)
    if (waves[k]) wave_follow(waves[k]);
}

UINT waveOutOpen(HWAVEOUT *h, UINT device, const WAVEFORMATEX *fmt, DWORD_PTR cb, DWORD_PTR inst, DWORD flags) {
  (void)device, (void)cb, (void)inst, (void)flags;
  if (!sdl_up()) return 1;
  SDL_AudioSpec spec = {SDL_AUDIO_S16, fmt->nChannels, (int)fmt->nSamplesPerSec};
  SDL_AudioStream *s = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, NULL, NULL);
  if (!s) return 1;
  SDL_ResumeAudioStreamDevice(s);
  Wave *w = calloc(1, sizeof *w);
  w->s = s;
  w->frame_bytes = fmt->nBlockAlign;
  for (int k = 0; k < 4; k++)
    if (!waves[k]) { waves[k] = w; break; }
  *h = w;
  return MMSYSERR_NOERROR;
}

UINT waveOutPrepareHeader(HWAVEOUT h, WAVEHDR *hdr, UINT size) { (void)h, (void)size; hdr->dwFlags |= WHDR_PREPARED; return 0; }

UINT waveOutWrite(HWAVEOUT h, WAVEHDR *hdr, UINT size) {
  (void)size;
  Wave *w = h;
  wave_follow(w);
  if (w->n == WAVE_MAX) { hdr->dwFlags |= WHDR_DONE; return 0; }
  SDL_PutAudioStreamData(w->s, hdr->lpData, (int)hdr->dwBufferLength);
  w->total += hdr->dwBufferLength;
  hdr->dwFlags &= ~(DWORD)WHDR_DONE;
  w->pend[w->n] = hdr;
  w->end[w->n++] = w->total;
  return 0;
}

UINT waveOutReset(HWAVEOUT h) {
  Wave *w = h;
  SDL_ClearAudioStream(w->s);
  for (int k = 0; k < w->n; k++) w->pend[k]->dwFlags |= WHDR_DONE;
  w->n = 0;
  w->total = 0;
  return 0;
}

UINT waveOutClose(HWAVEOUT h) {
  Wave *w = h;
  for (int k = 0; k < 4; k++)
    if (waves[k] == w) waves[k] = NULL;
  SDL_DestroyAudioStream(w->s);
  free(w);
  return 0;
}

// The menus' sounds: a WAV file in memory (PCM), played at once over the last one.
BOOL PlaySoundA(const char *sound, HMODULE m, DWORD flags) {
  (void)m, (void)flags;
  static SDL_AudioStream *s;
  if (!sdl_up()) return FALSE;
  if (s) SDL_ClearAudioStream(s);
  if (!sound) return TRUE;
  const uint8_t *p = (const uint8_t *)sound;
  if (memcmp(p, "RIFF", 4) || memcmp(p + 8, "WAVE", 4)) return FALSE;
  uint32_t size = *(const uint32_t *)(p + 4) + 8, at = 12;
  SDL_AudioSpec spec = {SDL_AUDIO_S16, 1, 22050};
  while (at + 8 <= size) {
    uint32_t n = *(const uint32_t *)(p + at + 4);
    if (!memcmp(p + at, "fmt ", 4)) { spec.channels = *(const uint16_t *)(p + at + 10); spec.freq = (int)*(const uint32_t *)(p + at + 12); }
    else if (!memcmp(p + at, "data", 4)) {
      if (!s) {
        s = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, NULL, NULL);
        if (!s) return FALSE;
        SDL_ResumeAudioStreamDevice(s);
      } else SDL_SetAudioStreamFormat(s, &spec, NULL);
      SDL_PutAudioStreamData(s, p + at + 8, (int)n);
      return TRUE;
    }
    at += 8 + n + (n & 1);
  }
  return FALSE;
}

// ---- the window: one, an SDL window; GDI draws into its picture, shown when the DC is given back.
// Its frame is counted as on Windows (8 pixels each side, 31 above), so that sizes asked for with
// AdjustWindowRect give the same inside.
#define FRAME_L 8
#define FRAME_T 31
#define FRAME_R 8
#define FRAME_B 8
typedef struct {
  SDL_Window *win;
  SDL_GLContext gl;  // OpenGL 3.3 (dl_gl.c draws the window), or NULL: an SDL renderer shows GDI's picture
  SDL_Renderer *ren;
  SDL_Texture *tex;
  int tw, th;
  HDC dc;
  HBITMAP fb;
  LONG style;
  bool focused;
} Win;
static Win *the;

// Icons: an SDL surface of the colour picture (its alpha not multiplied, as Windows' icons), for the window.
HICON CreateIconIndirect(ICONINFO *ii) {
  BITMAP b;
  if (!ii || !GetObjectA(ii->hbmColor, sizeof b, &b) || !b.bmBits) return NULL;
  SDL_Surface *s = SDL_CreateSurface(b.bmWidth, b.bmHeight, SDL_PIXELFORMAT_ARGB8888);
  if (!s) return NULL;
  for (int y = 0; y < b.bmHeight; y++) memcpy((uint8_t *)s->pixels + (size_t)y * s->pitch, (const uint8_t *)b.bmBits + (size_t)y * b.bmWidth * 4, (size_t)b.bmWidth * 4);
  return s;
}

LRESULT SendMessageW(HWND w, UINT msg, WPARAM wp, LPARAM lp) { // WM_SETICON: the window's icon
  if (msg == WM_SETICON && w && lp && wp == ICON_BIG) SDL_SetWindowIcon(((Win *)w)->win, (SDL_Surface *)lp);
  return 0;
}

static WNDPROC proc;

static MSG queue[256];
static int q_head, q_n;
static void post(UINT msg, WPARAM wp, LPARAM lp) {
  if (q_n == 256) return;
  queue[(q_head + q_n++) % 256] = (MSG){the, msg, wp, lp};
}

WORD RegisterClassW(const WNDCLASSW *wc) {
  proc = wc->lpfnWndProc;
  return 1;
}

HWND CreateWindowW(LPCWSTR cls, LPCWSTR title, DWORD style, int x, int y, int w, int h, HWND parent, void *menu, HINSTANCE inst, void *param) {
  (void)cls, (void)parent, (void)menu, (void)inst, (void)param;
  if (!sdl_up()) return NULL;
  char t[256];
  wide_to_utf8(title, -1, t, sizeof t);
  Win *win = calloc(1, sizeof *win);
  int cw = w - FRAME_L - FRAME_R, ch = h - FRAME_T - FRAME_B;
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
  SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
  win->win = SDL_CreateWindow(t, cw, ch, SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIDDEN | SDL_WINDOW_OPENGL);
  if (win->win && !(win->gl = SDL_GL_CreateContext(win->win))) { // no OpenGL 3.3: a window without it
    SDL_DestroyWindow(win->win);
    win->win = SDL_CreateWindow(t, cw, ch, SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIDDEN);
  }
  if (!win->win) { free(win); return NULL; }
  SDL_SetWindowPosition(win->win, x + FRAME_L, y + FRAME_T);
  if (win->gl) SDL_GL_MakeCurrent(win->win, win->gl);
  else win->ren = SDL_CreateRenderer(win->win, NULL);
  { // the program's icon: <program>.png next to it (tools/linux_package.sh puts it there)
    char p[MAX_PATH + 8];
    GetModuleFileNameA(NULL, p, MAX_PATH);
    strcat(p, ".png");
    SDL_Surface *icon = SDL_LoadPNG(p);
    if (icon) { SDL_SetWindowIcon(win->win, icon); SDL_DestroySurface(icon); }
  }
  win->style = (LONG)style;
  win->focused = true;
  SDL_StartTextInput(win->win);
  the = win;
  return win;
}

BOOL DestroyWindow(HWND h) {
  Win *w = h;
  if (!w) return FALSE;
  if (w->tex) SDL_DestroyTexture(w->tex);
  if (w->ren) SDL_DestroyRenderer(w->ren);
  if (w->gl) SDL_GL_DestroyContext(w->gl);
  SDL_DestroyWindow(w->win);
  if (w->dc) DeleteDC(w->dc);
  if (w->fb) DeleteObject(w->fb);
  if (the == w) the = NULL;
  free(w);
  return TRUE;
}

BOOL ShowWindow(HWND h, int how) { (void)how; return h && SDL_ShowWindow(((Win *)h)->win); }
LRESULT DefWindowProcW(HWND w, UINT msg, WPARAM wp, LPARAM lp) { (void)w, (void)msg, (void)wp, (void)lp; return 0; }
void PostQuitMessage(int code) { (void)code; }
BOOL InvalidateRect(HWND w, const RECT *r, BOOL erase) { (void)w, (void)r, (void)erase; post(WM_PAINT, 0, 0); return TRUE; }

HICON LoadIconW(HINSTANCE inst, LPCWSTR name) { (void)inst, (void)name; return NULL; }
HANDLE LoadCursorW(HINSTANCE inst, LPCWSTR name) { (void)inst, (void)name; return NULL; }

static float density(void) {
  float d = the ? SDL_GetWindowPixelDensity(the->win) : 1;
  return d > 0 ? d : 1;
}

BOOL GetClientRect(HWND h, RECT *r) {
  int w = 0, ht = 0;
  if (h) SDL_GetWindowSizeInPixels(((Win *)h)->win, &w, &ht);
  *r = (RECT){0, 0, w, ht};
  return TRUE;
}

BOOL GetWindowRect(HWND h, RECT *r) {
  int x = 0, y = 0, w = 0, ht = 0;
  SDL_GetWindowPosition(((Win *)h)->win, &x, &y);
  SDL_GetWindowSize(((Win *)h)->win, &w, &ht);
  *r = (RECT){x - FRAME_L, y - FRAME_T, x + w + FRAME_R, y + ht + FRAME_B};
  return TRUE;
}

BOOL AdjustWindowRect(RECT *r, DWORD style, BOOL menu) {
  (void)style, (void)menu;
  r->left -= FRAME_L, r->top -= FRAME_T, r->right += FRAME_R, r->bottom += FRAME_B;
  return TRUE;
}

BOOL SetWindowPos(HWND h, HWND after, int x, int y, int cx, int cy, UINT flags) {
  (void)after;
  Win *w = h;
  if (!w || (SDL_GetWindowFlags(w->win) & SDL_WINDOW_FULLSCREEN)) return TRUE;
  if (!(flags & SWP_NOSIZE)) SDL_SetWindowSize(w->win, cx - FRAME_L - FRAME_R, cy - FRAME_T - FRAME_B);
  if (!(flags & SWP_NOMOVE)) SDL_SetWindowPosition(w->win, x + FRAME_L, y + FRAME_T);
  SDL_SyncWindow(w->win);
  return TRUE;
}

HWND GetForegroundWindow(void) { return the && the->focused ? the : NULL; }

// The window's style: without its frame (WS_OVERLAPPEDWINDOW) it is fullscreen (dl_toggle_fullscreen).
LONG GetWindowLongW(HWND h, int index) { return index == GWL_STYLE && h ? ((Win *)h)->style : 0; }
LONG SetWindowLongW(HWND h, int index, LONG value) {
  Win *w = h;
  if (!w || index != GWL_STYLE) return 0;
  LONG old = w->style;
  w->style = value;
  bool was = old & WS_OVERLAPPEDWINDOW, now = value & WS_OVERLAPPEDWINDOW;
  if (was != now) SDL_SetWindowFullscreen(w->win, !now);
  return old;
}

BOOL GetWindowPlacement(HWND h, WINDOWPLACEMENT *p) {
  GetWindowRect(h, &p->rcNormalPosition);
  return TRUE;
}
BOOL SetWindowPlacement(HWND h, const WINDOWPLACEMENT *p) { (void)h, (void)p; return TRUE; } // SDL puts it back itself

static SDL_DisplayID display_of_window(void) { return the ? SDL_GetDisplayForWindow(the->win) : SDL_GetPrimaryDisplay(); }

HMONITOR MonitorFromWindow(HWND w, DWORD flags) { (void)w, (void)flags; return (HMONITOR)(uintptr_t)display_of_window(); }

HMONITOR MonitorFromRect(const RECT *r, DWORD flags) {
  int n = 0;
  SDL_DisplayID *ids = sdl_up() ? SDL_GetDisplays(&n) : NULL;
  HMONITOR m = NULL;
  for (int k = 0; k < n && !m; k++) {
    SDL_Rect b;
    if (SDL_GetDisplayBounds(ids[k], &b) && r->left < b.x + b.w && r->right > b.x && r->top < b.y + b.h && r->bottom > b.y) m = (HMONITOR)(uintptr_t)ids[k];
  }
  SDL_free(ids);
  return m || flags == MONITOR_DEFAULTTONULL ? m : (HMONITOR)(uintptr_t)SDL_GetPrimaryDisplay();
}

BOOL GetMonitorInfoW(HMONITOR m, MONITORINFO *mi) {
  SDL_Rect b;
  if (!SDL_GetDisplayBounds((SDL_DisplayID)(uintptr_t)m, &b)) return FALSE;
  mi->rcMonitor = mi->rcWork = (RECT){b.x, b.y, b.x + b.w, b.y + b.h};
  if (mi->cbSize >= sizeof(MONITORINFOEXW)) swprintf(((MONITORINFOEXW *)mi)->szDevice, 32, L"%u", (unsigned)(uintptr_t)m);
  return TRUE;
}

BOOL EnumDisplaySettingsW(const wchar_t *device, DWORD mode, DEVMODEW *dm) {
  (void)device, (void)mode;
  const SDL_DisplayMode *d = SDL_GetCurrentDisplayMode(display_of_window());
  dm->dmDisplayFrequency = d && d->refresh_rate > 1 ? (DWORD)(d->refresh_rate + 0.5f) : 60;
  return TRUE;
}

int GetSystemMetrics(int which) {
  if (which == SM_CXICON) return 32;
  if (which == SM_CXSMICON) return 16;
  SDL_Rect b = {0, 0, 1920, 1080};
  if (sdl_up()) SDL_GetDisplayBounds(SDL_GetPrimaryDisplay(), &b);
  return which == SM_CYSCREEN ? b.h : b.w;
}

// The window's DC: GDI draws into a picture of the window's size; giving it back shows it.
HDC GetDC(HWND h) {
  static HDC scratch;
  Win *w = h;
  if (!w) {
    if (!scratch) scratch = CreateCompatibleDC(NULL);
    return scratch;
  }
  int cw, ch, fw = 0, fh = 0;
  SDL_GetWindowSizeInPixels(w->win, &cw, &ch);
  if (w->fb) dl_gdi_bits(w->dc, &fw, &fh);
  if (!w->dc) w->dc = CreateCompatibleDC(NULL);
  if (!w->fb || fw != cw || fh != ch) {
    HBITMAP nb = CreateCompatibleBitmap(NULL, cw > 0 ? cw : 1, ch > 0 ? ch : 1);
    SelectObject(w->dc, nb);
    if (w->fb) DeleteObject(w->fb);
    w->fb = nb;
  }
  return w->dc;
}

SDL_Window *dl_win32_sdl_window(void) { return the && the->gl ? the->win : NULL; }
bool dl_gl_present(const uint32_t *px, int w, int h); // dl_gl.c

int ReleaseDC(HWND h, HDC dc) {
  Win *w = h;
  if (!w || dc != w->dc) return 1;
  int fw, fh;
  uint32_t *px = dl_gdi_bits(dc, &fw, &fh);
  if (w->gl) { // through OpenGL (nothing when the graphics card path shows the window)
    dl_gl_present(px, fw, fh);
    return 1;
  }
  if (!w->tex || w->tw != fw || w->th != fh) {
    if (w->tex) SDL_DestroyTexture(w->tex);
    w->tex = SDL_CreateTexture(w->ren, SDL_PIXELFORMAT_XRGB8888, SDL_TEXTUREACCESS_STREAMING, fw, fh);
    SDL_SetTextureScaleMode(w->tex, SDL_SCALEMODE_NEAREST);
    w->tw = fw, w->th = fh;
  }
  SDL_UpdateTexture(w->tex, NULL, px, fw * 4);
  SDL_RenderTexture(w->ren, w->tex, NULL, NULL);
  SDL_RenderPresent(w->ren);
  return 1;
}

HDC BeginPaint(HWND h, PAINTSTRUCT *ps) {
  memset(ps, 0, sizeof *ps);
  ps->hdc = GetDC(h);
  return ps->hdc;
}
BOOL EndPaint(HWND h, const PAINTSTRUCT *ps) { return ReleaseDC(h, ps->hdc); }

// ---- messages: SDL's events as Windows', for the window procedure
static void key_message(const SDL_KeyboardEvent *k) {
  const Key *key = key_of_sc(k->scancode);
  int vk = vk_of(k->scancode);
  if (!vk) return;
  LPARAM lp = (LPARAM)((key ? key->set1 : 0) << 16 | (key && key->ext ? 1 << 24 : 0) | (k->repeat ? 1 << 30 : 0) | 1);
  if ((k->mod & SDL_KMOD_ALT) && vk == VK_RETURN) post(WM_SYSKEYDOWN, VK_RETURN, lp);
  else if (!(k->mod & SDL_KMOD_ALT)) post(WM_KEYDOWN, (WPARAM)vk, lp);
}

static void pump(void) {
  SDL_Event e;
  float d = density();
  while (SDL_PollEvent(&e)) {
    switch (e.type) {
      case SDL_EVENT_QUIT:
      case SDL_EVENT_WINDOW_CLOSE_REQUESTED: post(WM_CLOSE, 0, 0); break;
      case SDL_EVENT_KEY_DOWN: key_message(&e.key); break;
      case SDL_EVENT_TEXT_INPUT: {
        wchar_t w[64];
        int n = utf8_to_wide(e.text.text, -1, w, 64) - 1;
        for (int k = 0; k < n; k++)
          if (w[k] >= 32) post(WM_CHAR, (WPARAM)w[k], 1);
        break;
      }
      case SDL_EVENT_MOUSE_MOTION: post(WM_MOUSEMOVE, 0, (LPARAM)(((int)(e.motion.x * d) & 0xFFFF) | ((int)(e.motion.y * d) & 0xFFFF) << 16)); break;
      case SDL_EVENT_MOUSE_BUTTON_DOWN: {
        LPARAM lp = (LPARAM)(((int)(e.button.x * d) & 0xFFFF) | ((int)(e.button.y * d) & 0xFFFF) << 16);
        if (e.button.button == SDL_BUTTON_LEFT) post(WM_LBUTTONDOWN, 1, lp);
        else if (e.button.button == SDL_BUTTON_RIGHT) post(WM_RBUTTONDOWN, 2, lp);
        break;
      }
      case SDL_EVENT_MOUSE_WHEEL: post(WM_MOUSEWHEEL, (WPARAM)((uint32_t)(uint16_t)(int16_t)(e.wheel.y * 120) << 16), 0); break;
      case SDL_EVENT_WINDOW_RESIZED:
      case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED: post(WM_SIZE, 0, 0); break;
      case SDL_EVENT_WINDOW_EXPOSED: post(WM_PAINT, 0, 0); break;
      case SDL_EVENT_WINDOW_FOCUS_GAINED: if (the) the->focused = true; break;
      case SDL_EVENT_WINDOW_FOCUS_LOST: if (the) the->focused = false; break;
      case SDL_EVENT_GAMEPAD_ADDED: dl_pads_added(e.gdevice.which); post(WM_DEVICECHANGE, 7, 0); break;
      case SDL_EVENT_GAMEPAD_REMOVED: dl_pads_removed(e.gdevice.which); post(WM_DEVICECHANGE, 7, 0); break;
    }
  }
}

// For tests: DL_TEST_KEYS="28 28 0D" plays those keys (virtual-key codes in hexadecimal) to this window,
// one every 400 ms after a first second; nothing reaches any other window.
static void test_keys(void) {
  static const char *next;
  static Uint64 at;
  static bool started;
  if (!started) {
    started = true;
    next = getenv("DL_TEST_KEYS");
    at = SDL_GetTicks() + 1000;
  }
  if (!next || !the || SDL_GetTicks() < at) return;
  while (*next == ' ') next++;
  if (!*next) { next = NULL; return; }
  char *end;
  long vk = strtol(next, &end, 16);
  next = end;
  if (vk > 0) post(WM_KEYDOWN, (WPARAM)vk, 1);
  at = SDL_GetTicks() + 400;
}

BOOL PeekMessageW(MSG *m, HWND w, UINT lo, UINT hi, UINT remove) {
  (void)w, (void)lo, (void)hi, (void)remove;
  audio_follow();
  if (!q_n) test_keys();
  if (!q_n && sdl_up()) pump();
  if (!q_n) return FALSE;
  *m = queue[q_head];
  q_head = (q_head + 1) % 256;
  q_n--;
  return TRUE;
}

BOOL TranslateMessage(const MSG *m) { (void)m; return TRUE; } // WM_CHAR comes from SDL's text events
LRESULT DispatchMessageW(const MSG *m) { return proc && m->hwnd ? proc(m->hwnd, m->message, m->wParam, m->lParam) : 0; }
