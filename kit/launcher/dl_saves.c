// Save states, screenshots and the notes shown over the game, taken from Kahna.
//
// Save states: ten slots (by default in the "saves" folder, <FILE_STEM>.state0 to .state9; the game may
// say where), each with the picture of the game when it was saved beside it (.png) and maybe a name
// (.name); the game's automatic states; the session kept at closing time; the rewind. The game writes
// and reads the state itself (DlGame.save_state, load_state, state_to_mem...). Screenshots: PNG files in the "captures" folder, the picture as it is
// shown (filters included) when the graphics card draws it, the game's own picture otherwise. Both
// have a page of the setup screen: a list, the picture of the selected row instead of the live preview.
#include "dl_int.h"
#include <shellapi.h>
#include <time.h>
#include <wctype.h>

int dl_slot;
bool dl_state_taken; // a state was loaded since the game was last started or stopped

// ---- notes over the game ("state 3 saved"...), for a few seconds
static wchar_t note_text[160];
static DWORD note_until;

void dl_notef(const wchar_t *fmt, int n) {
  if (dl_running_ahead()) return;
  _snwprintf(note_text, 159, fmt, n);
  note_text[159] = 0;
  note_until = GetTickCount() + 2500;
  if (!note_until) note_until = 1;
  dl_dirty = true;
}

void dl_note(const wchar_t *text) {
  if (dl_running_ahead()) return;
  lstrcpynW(note_text, text, 160);
  note_until = GetTickCount() + 2500;
  if (!note_until) note_until = 1;
  dl_dirty = true;
}

const wchar_t *dl_note_now(void) {
  if (note_until && (int)(note_until - GetTickCount()) > 0) return note_text;
  note_until = 0;
  return NULL;
}

static HFONT note_font(int h) {
  static HFONT f;
  static int fh;
  int want = h / 22 < 14 ? 14 : h / 22;
  if (want != fh) { if (f) DeleteObject(f); fh = want; f = CreateFontW(-fh, 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, ANTIALIASED_QUALITY, 0, dl_cfg.ui_lang ? L"Verdana" : L"Yu Gothic UI"); }
  return f;
}

// Size of the note's box for a picture h pixels high.
void dl_note_size(HDC dc, int h, int *w, int *bh) {
  const wchar_t *s = dl_note_now();
  int fh = h / 22 < 14 ? 14 : h / 22;
  SIZE sz = {0, 0};
  if (s) {
    HGDIOBJ of = SelectObject(dc, note_font(h));
    GetTextExtentPoint32W(dc, s, lstrlenW(s), &sz);
    SelectObject(dc, of);
  }
  *w = sz.cx + fh;
  *bh = fh * 2;
}

// The note in its box, whose top left corner is x,y (a darkened band, then the text).
void dl_draw_note(HDC dc, int x, int y, int h) {
  const wchar_t *s = dl_note_now();
  if (!s) return;
  int w, bh, fh = h / 22 < 14 ? 14 : h / 22;
  dl_note_size(dc, h, &w, &bh);
  static HDC src;
  static u32 *px;
  if (!src) {
    BITMAPINFO bi = {{sizeof(BITMAPINFOHEADER), 1, 1, 1, 32}};
    src = CreateCompatibleDC(dc);
    SelectObject(src, CreateDIBSection(src, &bi, DIB_RGB_COLORS, (void **)&px, NULL, 0));
  }
  COLORREF band = ui_tint(RGB(0x0A, 0x1A, 0x40));
  *px = (u32)GetRValue(band) << 16 | (u32)GetGValue(band) << 8 | GetBValue(band);
  BLENDFUNCTION bf = {AC_SRC_OVER, 0, 170, 0};
  AlphaBlend(dc, x, y, w, bh, src, 0, 0, 1, 1, bf);
  HGDIOBJ of = SelectObject(dc, note_font(h));
  SetBkMode(dc, TRANSPARENT);
  SetTextAlign(dc, TA_LEFT | TA_TOP);
  SetTextColor(dc, RGB(0, 0, 0));
  TextOutW(dc, x + fh / 2 + 1, y + fh / 2 + 1, s, lstrlenW(s));
  SetTextColor(dc, RGB(255, 255, 255));
  TextOutW(dc, x + fh / 2, y + fh / 2, s, lstrlenW(s));
  SelectObject(dc, of);
}

// ---- pictures read from files (GDI+, loaded on first use)
typedef struct { UINT32 version; void *debug_cb; BOOL no_thread; BOOL no_codecs; } GpStartupInput;
HBITMAP dl_picture_load(const char *path, int *w, int *h) {
  static bool tried;
  static int(WINAPI * from_file)(const WCHAR *, void **);
  static int(WINAPI * to_hbitmap)(void *, HBITMAP *, DWORD);
  static int(WINAPI * dispose)(void *);
  if (!tried) {
    tried = true;
    HMODULE gp = LoadLibraryA("gdiplus.dll");
    int(WINAPI * startup)(ULONG_PTR *, const void *, void *) = NULL; // GdiplusStartup (the input is a GpStartupInput)
    if (gp) {
      startup = (void *)GetProcAddress(gp, "GdiplusStartup");
      from_file = (void *)GetProcAddress(gp, "GdipCreateBitmapFromFile");
      to_hbitmap = (void *)GetProcAddress(gp, "GdipCreateHBITMAPFromBitmap");
      dispose = (void *)GetProcAddress(gp, "GdipDisposeImage");
    }
    ULONG_PTR token;
    GpStartupInput in = {1, NULL, FALSE, FALSE};
    if (!startup || !from_file || !to_hbitmap || !dispose || startup(&token, &in, NULL) != 0) from_file = NULL;
  }
  *w = *h = 0;
  if (!from_file) return NULL;
  wchar_t wp[MAX_PATH];
  MultiByteToWideChar(CP_ACP, 0, path, -1, wp, MAX_PATH);
  void *img = NULL;
  HBITMAP out = NULL;
  if (from_file(wp, &img) == 0 && img) {
    to_hbitmap(img, &out, 0xFF000000);
    dispose(img); // the file is not kept open: it can be deleted
  }
  BITMAP bm;
  if (out && GetObjectA(out, sizeof bm, &bm)) { *w = bm.bmWidth; *h = bm.bmHeight; }
  return out;
}

// A folder beside the program, made only when something is written there.
static void folder(char *out, const char *name, bool make) {
  snprintf(out, MAX_PATH, "%s%s", dl_base_dir, name);
  if (make) CreateDirectoryA(out, NULL);
}

// The folder of a file, made if it is not there yet (one level).
static void make_parent(const char *path) {
  char dir[MAX_PATH];
  snprintf(dir, sizeof dir, "%s", path);
  char *slash = strrchr(dir, DL_SEPC);
  if (slash) { *slash = 0; CreateDirectoryA(dir, NULL); }
}

static bool file_exists(const char *p) { return GetFileAttributesA(p) != INVALID_FILE_ATTRIBUTES; }

// ---- save states: n 0-9 the player's slots, DL_SLOTS + k the game's automatic states, -1 the session
void dl_slot_path(char *out, int n, const char *ext) {
  char base[MAX_PATH];
  if (dl_game->state_path) dl_game->state_path(n, base);
  else {
    char dir[MAX_PATH];
    folder(dir, "saves", false);
    if (n < 0) snprintf(base, sizeof base, "%s" DL_SEP FILE_STEM ".session", dir);
    else if (n >= DL_SLOTS) snprintf(base, sizeof base, "%s" DL_SEP FILE_STEM ".auto%d", dir, n - DL_SLOTS);
    else snprintf(base, sizeof base, "%s" DL_SEP FILE_STEM ".state%d", dir, n);
  }
  snprintf(out, MAX_PATH, "%s%s", base, ext);
}

bool dl_slot_used(int n) {
  char p[MAX_PATH];
  dl_slot_path(p, n, "");
  return file_exists(p);
}

void dl_state_loaded(void) {
  dl_state_taken = true;
  if (dl_game->state_loaded) dl_game->state_loaded();
}

bool dl_state_save(int n) {
  char p[MAX_PATH], pic[MAX_PATH];
  dl_slot_path(p, n, "");
  dl_slot_path(pic, n, ".png");
  make_parent(p);
  dl_backup(p);
  if (!dl_game->save_state || !dl_game->save_state(p)) return false;
  dl_save_png(pic, dl_pic, dl_pic_w, dl_pic_h);
  return true;
}

bool dl_state_load(int n) {
  char p[MAX_PATH];
  dl_slot_path(p, n, "");
  if (!dl_game->load_state || !file_exists(p) || !dl_game->load_state(p)) return false;
  dl_state_loaded();
  return true;
}

void dl_quick_save(void) {
  if (!dl_game->save_state) return;
  dl_notef(dl_state_save(dl_slot) ? TX(T_M_SAVED) : TX(T_M_SAVE_FAIL), dl_slot);
}

void dl_quick_load(void) {
  if (!dl_game->load_state) return;
  if (!dl_slot_used(dl_slot)) dl_notef(TX(T_M_SLOT_EMPTY), dl_slot);
  else dl_notef(dl_state_load(dl_slot) ? TX(T_M_LOADED) : TX(T_M_LOAD_FAIL), dl_slot);
}

void dl_slot_step(int dir) {
  dl_slot = (dl_slot + DL_SLOTS + dir) % DL_SLOTS;
  dl_notef(dl_slot_used(dl_slot) ? TX(T_M_SLOT) : TX(T_M_SLOT_FREE), dl_slot);
}

// ---- the session: the game in progress kept when the program closes, taken up by "Continue"
bool dl_session_exists(void) {
  if (!dl_game->resume || !dl_game->save_state || !dl_game->load_state) return false;
  char p[MAX_PATH];
  dl_slot_path(p, -1, "");
  return file_exists(p);
}

bool dl_session_resume(void) {
  if (!dl_session_exists()) return false;
  char p[MAX_PATH];
  dl_slot_path(p, -1, "");
  if (!dl_game->load_state(p)) return false;
  dl_state_loaded();
  dl_play();
  dl_note(TX(T_M_RESUMED));
  return true;
}

void dl_session_keep(void) {
  if (!dl_game->resume || !dl_game->save_state || !dl_started) return;
  char p[MAX_PATH];
  dl_slot_path(p, -1, "");
  make_parent(p);
  dl_backup(p);
  dl_game->save_state(p);
}

// The help of "Continue": when the session dates from, and the time played if the game counts it.
// The newest of the session and the game's automatic states: -1 the session, DL_SLOTS + k an automatic
// state, -2 none.
int dl_latest_auto(void) {
  if (!dl_game->load_state) return -2;
  int best = -2;
  FILETIME best_t = {0, 0};
  for (int i = -1; i < dl_game->auto_slots && i < 8; i++) {
    int n = i < 0 ? -1 : DL_SLOTS + i;
    if (n < 0 && !dl_game->resume) continue;
    char p[MAX_PATH];
    dl_slot_path(p, n, "");
    WIN32_FILE_ATTRIBUTE_DATA a;
    if (!GetFileAttributesExA(p, GetFileExInfoStandard, &a)) continue;
    if (best == -2 || CompareFileTime(&a.ftLastWriteTime, &best_t) > 0) { best = n; best_t = a.ftLastWriteTime; }
  }
  return best;
}

const wchar_t *dl_session_help(void) {
  static wchar_t out[1024];
  char p[MAX_PATH];
  WIN32_FILE_ATTRIBUTE_DATA fa;
  dl_slot_path(p, -1, "");
  lstrcpynW(out, TX(T_H_CONTINUE), 1024);
  if (!GetFileAttributesExA(p, GetFileExInfoStandard, &fa)) return out;
  FILETIME lt;
  SYSTEMTIME st;
  wchar_t day[48], hour[24], when[80], tail[256];
  FileTimeToLocalFileTime(&fa.ftLastWriteTime, &lt);
  FileTimeToSystemTime(&lt, &st);
  GetDateFormatW(LOCALE_USER_DEFAULT, DATE_SHORTDATE, &st, NULL, day, 48);
  GetTimeFormatW(LOCALE_USER_DEFAULT, TIME_NOSECONDS, &st, NULL, hour, 24);
  _snwprintf(when, 79, L"%ls %ls", day, hour);
  when[79] = 0;
  if (dl_game->play_seconds) {
    int secs = dl_game->play_seconds();
    _snwprintf(tail, 255, TX(T_SESSION), when, secs / 3600, secs / 60 % 60);
  } else _snwprintf(tail, 255, TX(T_SESSION_DATE), when);
  tail[255] = 0;
  _snwprintf(out, 1023, L"%ls%ls", TX(T_H_CONTINUE), tail);
  out[1023] = 0;
  return out;
}

// ---- rewind: a snapshot every 6 pictures, the last 200 of them (about 20 seconds at 60 pictures a second)
#define RW_SLOTS 200
static u8 *rw_buf;
static size_t rw_size;
static int rw_head, rw_count, rw_total, rw_tick;
static DWORD rw_last;
static bool rw_used;

bool dl_rewind_ok(void) { return dl_game->state_size && dl_game->state_to_mem && dl_game->state_from_mem; }

void dl_rewind_snap(void) {
  if (!dl_rewind_ok() || ++rw_tick % 6) return;
  size_t need = dl_game->state_size();
  if (need != rw_size || !rw_buf) {
    free(rw_buf);
    rw_size = need;
    rw_buf = need ? malloc(need * RW_SLOTS) : NULL;
    rw_head = rw_count = 0;
    if (!rw_buf) { rw_size = 0; return; }
  }
  dl_game->state_to_mem(rw_buf + (size_t)rw_head * rw_size);
  rw_head = (rw_head + 1) % RW_SLOTS;
  if (rw_count < RW_SLOTS) rw_count++;
  rw_total = rw_count;
}

// One step back every 90 ms while the key is held; true when there is a new picture to show.
bool dl_rewind_step(void) {
  static s16 sink[8192];
  DWORD t = GetTickCount();
  if (!rw_buf || rw_count <= 0 || t - rw_last < 90) return false;
  rw_head = (rw_head + RW_SLOTS - 1) % RW_SLOTS;
  rw_count--;
  const u8 *snap = rw_buf + (size_t)rw_head * rw_size;
  dl_game->state_from_mem(snap);
  dl_game->frame(&(DlInput){0}, dl_pic, sink, 4096); // the picture of that moment
  dl_game->state_from_mem(snap);
  dl_notef(TX(T_M_REWIND), (rw_total - rw_count) * 6 / 60);
  rw_last = t;
  rw_used = true;
  return true;
}

void dl_rewind_end(void) {
  if (!rw_used) return;
  rw_used = false;
  rw_total = rw_count; // the timeline goes on from here
  dl_state_loaded();
  dl_input_release();
}

// ---- screenshots
void dl_screenshot(void) {
  char dir[MAX_PATH], p[MAX_PATH];
  folder(dir, "captures", true);
  SYSTEMTIME t;
  GetLocalTime(&t);
  snprintf(p, sizeof p, "%s" DL_SEP FILE_STEM "_%04d%02d%02d_%02d%02d%02d.png", dir, t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond);
  for (int k = 2; file_exists(p) && k < 100; k++)
    snprintf(p, sizeof p, "%s" DL_SEP FILE_STEM "_%04d%02d%02d_%02d%02d%02d_%d.png", dir, t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond, k);
  bool ok = false;
  if (dl_gpu_ready()) { // the picture as shown, drawn once more for it
    snprintf(dl_shot_path, sizeof dl_shot_path, "%s", p);
    dl_shot_game = true;
    dl_shot_ok = false;
    ok = dl_gpu_show(true, false) && dl_shot_ok;
    dl_shot_path[0] = 0;
    dl_shot_game = false;
  }
  if (!ok) ok = dl_save_png(p, dl_pic, dl_pic_w, dl_pic_h);
  dl_notef(ok ? TX(T_M_SHOT) : TX(T_M_SHOT_FAIL), 0);
}

// ---- the pages: lists drawn by dl_menu.c
static HBITMAP pic_bmp; // the picture of the selected row, from a file
static int pic_w, pic_h, pic_for = -1;
static u32 *state_px;   // or made by loading the state for one picture
static int state_w, state_h, state_for = -1;
static double state_aspect;
static int confirm = -1; // row waiting for the second press of Delete

static void pic_forget(void) {
  if (pic_bmp) DeleteObject(pic_bmp);
  pic_bmp = NULL;
  pic_w = pic_h = 0;
  pic_for = state_for = -1;
}

static HBITMAP pic_of(const char *path, int row, int *w, int *h) {
  if (pic_for != row) {
    if (pic_bmp) DeleteObject(pic_bmp);
    pic_bmp = file_exists(path) ? dl_picture_load(path, &pic_w, &pic_h) : NULL;
    pic_for = row;
  }
  *w = pic_w;
  *h = pic_h;
  return pic_bmp;
}

static void row_date(const FILETIME *ft, wchar_t *out, bool seconds) {
  FILETIME lt;
  SYSTEMTIME st;
  FileTimeToLocalFileTime(ft, &lt);
  FileTimeToSystemTime(&lt, &st);
  bool month_first = dl_cfg.ui_lang == 0 || dl_cfg.ui_lang == 2; // Japanese and English
  int a = month_first ? st.wMonth : st.wDay, b = month_first ? st.wDay : st.wMonth;
  if (seconds) wsprintfW(out, L"%02d/%02d/%04d  %02d:%02d:%02d", a, b, st.wYear, st.wHour, st.wMinute, st.wSecond);
  else wsprintfW(out, L"%02d/%02d  %02d:%02d", a, b, st.wHour, st.wMinute);
}

static void empty_text(HDC dc, double x0, double y0, double x1, double y1) {
  ui_text(dc, f_item, (x0 + x1) / 2, (y0 + y1) / 2 - 8, TX(T_EMPTY), ui_tint(RGB(110, 124, 210)), TA_CENTER);
}

// save states: the ten slots, then the game's automatic ones
#define SV_MAX (DL_SLOTS + 8)
#define NAME_MAX 28
static bool sv_used[SV_MAX];
static FILETIME sv_time[SV_MAX];
static wchar_t sv_name[DL_SLOTS][NAME_MAX + 1], edit_buf[NAME_MAX + 1];
static int edit_slot = -1; // slot whose name is being typed (F2)

// ---- the keyboard on screen, for a slot's name with a controller (X): three rows of the page of keys
// in hand, then the page, capitals, space, erase and OK, two cells each
static bool osk;
static int osk_x, osk_y, osk_page;
static bool osk_caps;
#define OSK_PAGES 6
static const wchar_t *const osk_rows[OSK_PAGES][3] = {
    {L"abcdefghij", L"klmnopqrst", L"uvwxyz-'.!"},
    {L"1234567890", L"&@#+=?:;()", L"/,_~*%$<>\u20AC"},
    {L"\u00E0\u00E2\u00E4\u00E7\u00E9\u00E8\u00EA\u00EB\u00EE\u00EF", L"\u00F4\u00F6\u00F9\u00FB\u00FC\u00F1\u00DF\u00E3\u00F5\u00E1",
     L"\u00ED\u00F3\u00FA\u00EC\u00F2\u00E5\u00E6\u00F8\u0153\u00BF"},
    // the kana: a-ho, ma-n with the long mark and small ya yu yo, then the voiced ones and the small ones
    {L"\u3042\u3044\u3046\u3048\u304A\u304B\u304D\u304F\u3051\u3053", L"\u3055\u3057\u3059\u305B\u305D\u305F\u3061\u3064\u3066\u3068",
     L"\u306A\u306B\u306C\u306D\u306E\u306F\u3072\u3075\u3078\u307B"},
    {L"\u307E\u307F\u3080\u3081\u3082\u3084\u3086\u3088\u3089\u308A", L"\u308B\u308C\u308D\u308F\u3092\u3093\u30FC\u3083\u3085\u3087",
     L"\u304C\u304E\u3050\u3052\u3054\u3056\u3058\u305A\u305C\u305E"},
    {L"\u3060\u3062\u3065\u3067\u3069\u3070\u3073\u3076\u3079\u307C", L"\u3071\u3074\u3077\u307A\u307D\u3041\u3043\u3045\u3047\u3049",
     L"\u3063\u308E\u3001\u3002\u30FB\uFF01\uFF1F\u300C\u300D\u301C"},
};
static const wchar_t *const osk_page_names[OSK_PAGES] = {L"abc", L"123", L"\u00E0\u00E9", L"\u3042", L"\u307E", L"\u3060"};

// The controller's buttons as the help names them: A B X Y LB RB, or a PlayStation pad's.
static void pad_names(const wchar_t **a, const wchar_t **b, const wchar_t **x, const wchar_t **y, const wchar_t **lb, const wchar_t **rb) {
  bool ps = dl_input_kind == DL_IN_PS || dl_input_kind == DL_IN_PS_HID;
  *a = ps ? TX(T_PS_CROSS) : L"A";
  *b = ps ? TX(T_PS_CIRCLE) : L"B";
  *x = ps ? TX(T_PS_SQUARE) : L"X";
  *y = ps ? TX(T_PS_TRIANGLE) : L"Y";
  *lb = ps ? L"L1" : L"LB";
  *rb = ps ? L"R1" : L"RB";
}

// The character of a key, with capitals (Latin letters) or katakana (kana); 0 for none.
static wchar_t osk_char(int page, int x, int y) {
  wchar_t c = osk_rows[page][y][x];
  if (!osk_caps) return c;
  if (c >= 0x3041 && c <= 0x3096) return (wchar_t)(c + 0x60); // hiragana to katakana
  if (c == 0x00DF) return c;
  return (wchar_t)towupper(c);
}

static void osk_press(void);

static int states_count(void) { return DL_SLOTS + (dl_game->auto_slots < 8 ? dl_game->auto_slots : 8); }
// then the row opening a page of the game (dl_saves_more), if it has one
static int more_page = -1;
static const wchar_t *const *more_label, *const *more_help;
void dl_saves_more(int page, const wchar_t *const *label, const wchar_t *const *help) {
  more_page = page;
  more_label = label;
  more_help = help;
}
// then "Backups" (the page of the copies kept before the saves were written over)
static bool is_backups(int k) { return k == states_count(); }
static bool is_more(int k) { return more_page >= 0 && k == states_count() + 1; }
static int saves_count(void) { return states_count() + 1 + (more_page >= 0); }

// A slot's name is kept beside it in "<state>.name" (UTF-8).
static void name_read(int n, wchar_t *out) {
  char p[MAX_PATH], s[NAME_MAX * 4 + 1];
  dl_slot_path(p, n, ".name");
  out[0] = 0;
  FILE *f = fopen(p, "rb");
  if (!f) return;
  size_t len = fread(s, 1, sizeof s - 1, f);
  fclose(f);
  s[len] = 0;
  if (!MultiByteToWideChar(CP_UTF8, 0, s, -1, out, NAME_MAX + 1)) out[0] = 0;
  out[NAME_MAX] = 0;
}

static void name_write(int n, const wchar_t *name) {
  char p[MAX_PATH], s[NAME_MAX * 4 + 1];
  dl_slot_path(p, n, ".name");
  int len = name[0] ? WideCharToMultiByte(CP_UTF8, 0, name, -1, s, sizeof s, NULL, NULL) : 0;
  if (len <= 1) { DeleteFileA(p); return; } // no name any more
  FILE *f = fopen(p, "wb");
  if (f) { fwrite(s, 1, (size_t)len - 1, f); fclose(f); }
}

static void saves_open(void) {
  pic_forget();
  confirm = edit_slot = -1;
  for (int k = 0; k < states_count(); k++) {
    char p[MAX_PATH];
    WIN32_FILE_ATTRIBUTE_DATA fa;
    dl_slot_path(p, k, "");
    sv_used[k] = GetFileAttributesExA(p, GetFileExInfoStandard, &fa) != 0;
    if (sv_used[k]) sv_time[k] = fa.ftLastWriteTime;
    if (k < DL_SLOTS) name_read(k, sv_name[k]);
  }
}
static void saves_row(int k, wchar_t *label, wchar_t *value) {
  if (is_backups(k)) {
    lstrcpynW(label, TX(T_BACKUPS), 64);
    value[0] = 0;
    return;
  }
  if (is_more(k)) {
    lstrcpynW(label, dl_tr(more_label), 64);
    value[0] = 0;
    return;
  }
  if (k == edit_slot) _snwprintf(label, 63, L"%d. %ls_", k, edit_buf);
  else if (k < DL_SLOTS && sv_name[k][0]) _snwprintf(label, 63, L"%d. %ls", k, sv_name[k]);
  else if (k < DL_SLOTS) wsprintfW(label, TX(T_SLOT_N), k);
  else if (dl_game->auto_labels) lstrcpynW(label, dl_tr(dl_game->auto_labels[k - DL_SLOTS]), 64);
  else wsprintfW(label, L"Auto %d", k - DL_SLOTS + 1);
  label[63] = 0;
  if (sv_used[k]) row_date(&sv_time[k], value, false);
  else lstrcpyW(value, TX(T_EMPTY));
}
static const wchar_t *saves_help(int k) {
  static wchar_t buf[512];
  const wchar_t *pa, *pb, *px, *py, *plb, *prb;
  pad_names(&pa, &pb, &px, &py, &plb, &prb);
  if (edit_slot >= 0 && osk) {
    _snwprintf(buf, 511, TX(T_SVH_OSK), pa, pb, py, plb, prb, px);
    buf[511] = 0;
    return buf;
  }
  if (edit_slot >= 0) return TX(T_SVH_NAME);
  if (is_more(k)) return more_help ? dl_tr(more_help) : L"";
  if (is_backups(k)) return TX(T_H_BACKUPS);
  if (confirm == k) return TX(T_SVH_CONFIRM);
  if (k >= DL_SLOTS) return sv_used[k] ? TX(T_SVH_AUTO) : TX(T_SVH_AUTO_EMPTY);
  if (!sv_used[k]) return dl_started ? TX(T_SVH_SAVE) : TX(T_SVH_EMPTY);
  wchar_t named[160];
  _snwprintf(named, 159, TX(T_SVH_X), px);
  named[159] = 0;
  _snwprintf(buf, 511, L"%ls%ls", TX(T_SVH_LOAD), dl_input_kind == DL_IN_KEYS ? TX(T_SVH_F2) : named);
  buf[511] = 0;
  return buf;
}

// The picture of a state saved without one: the state is loaded for one picture of the game, then
// everything is put back as it was.
static bool state_picture(int k) {
  if (state_for == k) return state_px != NULL;
  state_for = k;
  if (!dl_rewind_ok() || !dl_game->load_state) return false;
  size_t n = dl_game->state_size(), px_n = (size_t)dl_game->width * dl_game->height;
  u8 *keep = malloc(n);
  u32 *keep_px = malloc(px_n * 4);
  if (!state_px) state_px = malloc(px_n * 4);
  bool ok = false;
  if (keep && keep_px && state_px) {
    static s16 sink[8192];
    int w = dl_pic_w, h = dl_pic_h;
    double a = dl_pic_aspect;
    char p[MAX_PATH];
    dl_game->state_to_mem(keep);
    memcpy(keep_px, dl_pic, px_n * 4);
    dl_slot_path(p, k, "");
    if (dl_game->load_state(p)) {
      dl_game->frame(&(DlInput){0}, dl_pic, sink, 4096);
      state_w = dl_pic_w;
      state_h = dl_pic_h;
      state_aspect = dl_pic_aspect;
      memcpy(state_px, dl_pic, (size_t)state_w * state_h * 4);
      ok = true;
    }
    dl_game->state_from_mem(keep);
    memcpy(dl_pic, keep_px, px_n * 4);
    dl_pic_w = w;
    dl_pic_h = h;
    dl_pic_aspect = a;
  }
  free(keep);
  free(keep_px);
  return ok;
}

// The keyboard on screen, in the place of the slot's picture.
static void osk_draw(HDC dc, double x0, double y0, double x1, double y1) {
  double cw = (x1 - x0 - 8) / 10, ch = (y1 - y0 - 8) / 4;
  for (int y = 0; y < 4; y++)
    for (int x = 0; x < 10; x += y < 3 ? 1 : 2) {
      double cx0 = x0 + 4 + x * cw, cy0 = y0 + 4 + y * ch, cx1 = cx0 + cw * (y < 3 ? 1 : 2) - 2, cy1 = cy0 + ch - 2;
      bool on = osk_y == y && (y < 3 ? osk_x == x : osk_x / 2 == x / 2);
      if (on) ui_cursor(dc, cx0, cy0, cx1, cy1);
      else ui_box(dc, cx0, cy0, cx1, cy1, ui_tint(RGB(6, 18, 58)), 3);
      wchar_t label[8] = {0};
      if (y < 3) label[0] = osk_char(osk_page, x, y);
      else if (x == 0) lstrcpyW(label, osk_page_names[(osk_page + 1) % OSK_PAGES]);
      else if (x == 2) lstrcpyW(label, osk_caps ? L"aA" : L"Aa");
      else if (x == 4) lstrcpyW(label, L"_");
      else if (x == 6) lstrcpyW(label, L"\u2190");
      else lstrcpyW(label, L"OK");
      ui_text(dc, f_small, (cx0 + cx1) / 2, (cy0 + cy1) / 2 - 7, label, on ? RGB(255, 255, 255) : COL_VALUE, TA_CENTER);
    }
}

static bool saves_preview(void *dcv, int k, double x0, double y0, double x1, double y1) {
  HDC dc = dcv;
  if (osk && edit_slot >= 0) { osk_draw(dc, x0, y0, x1, y1); return true; }
  char p[MAX_PATH];
  int w, h;
  if (is_more(k) || is_backups(k)) return false; // the live picture
  if (!sv_used[k]) { empty_text(dc, x0, y0, x1, y1); return true; }
  dl_slot_path(p, k, ".png");
  HBITMAP bmp = pic_of(p, k, &w, &h);
  if (bmp && w > 0 && h > 0) dl_draw_picture(dc, bmp, w, h, x0, y0, x1, y1);
  else if (state_picture(k)) dl_draw_pixels(dc, state_px, state_w, state_h, state_aspect, x0, y0, x1, y1);
  return true;
}
static void saves_enter(int k) {
  confirm = -1;
  if (is_more(k)) { dl_open_page(more_page); return; }
  if (is_backups(k)) { dl_open_page(PG_BACKUPS); return; }
  if (sv_used[k]) { // load it and play
    if (k < DL_SLOTS) dl_slot = k;
    if (!dl_state_load(k)) { dl_notef(TX(T_M_LOAD_FAIL), k); return; }
    dl_play();
    dl_notef(TX(T_M_LOADED), k);
  } else if (dl_started && k < DL_SLOTS) { // a free slot: the game in progress goes there
    dl_slot = k;
    dl_state_save(k);
    saves_open();
  }
}
static void saves_erase(int k) {
  if (is_more(k) || is_backups(k) || !sv_used[k]) return;
  if (confirm != k) { confirm = k; return; }
  static const char *const ext[] = {"", ".png", ".name"};
  for (int e = 0; e < 3; e++) {
    char p[MAX_PATH];
    dl_slot_path(p, k, ext[e]);
    DeleteFileA(p);
  }
  saves_open();
}
static void list_moved(void) { confirm = -1; }

// Naming a slot: F2 on a slot in use, then the keys of the name, Enter to keep it, Esc to cancel.
bool dl_saves_key(int vk) {
  if (edit_slot < 0) {
    if (vk != VK_F2 || dl_sel >= DL_SLOTS || !sv_used[dl_sel]) return false;
    edit_slot = dl_sel;
    lstrcpyW(edit_buf, sv_name[dl_sel]);
    confirm = -1;
    osk = dl_input_kind != DL_IN_KEYS; // from a controller: the keyboard on screen
    osk_x = osk_y = 0;
    dl_dirty = true;
    return true;
  }
  int len = lstrlenW(edit_buf);
  if (osk) { // A types the key in hand, B erases (and leaves when there is nothing more), X keeps the name
    switch (vk) {
      case VK_LEFT: osk_x = osk_y < 3 ? (osk_x + 9) % 10 : (osk_x / 2 * 2 + 8) % 10; break;
      case VK_RIGHT: osk_x = osk_y < 3 ? (osk_x + 1) % 10 : (osk_x / 2 * 2 + 2) % 10; break;
      case VK_UP: osk_y = (osk_y + 3) % 4; break;
      case VK_DOWN: osk_y = (osk_y + 1) % 4; break;
      case VK_RETURN: osk_press(); break;
      case VK_ESCAPE:
        if (len) edit_buf[len - 1] = 0;
        else osk = false, edit_slot = -1;
        break;
      case VK_DELETE: osk_caps = !osk_caps; break;
      case VK_PRIOR: osk_page = (osk_page + OSK_PAGES - 1) % OSK_PAGES; break;
      case VK_NEXT: osk_page = (osk_page + 1) % OSK_PAGES; break;
      case VK_F2: vk = VK_RETURN; osk = false; break; // keep the name, as Enter does on the keyboard
      default: break;
    }
    if (osk) { dl_dirty = true; return true; }
  }
  if (vk == VK_RETURN) {
    while (len && edit_buf[len - 1] == L' ') edit_buf[--len] = 0;
    lstrcpyW(sv_name[edit_slot], edit_buf);
    name_write(edit_slot, edit_buf);
    edit_slot = -1;
    dl_beep(1);
  } else if (vk == VK_ESCAPE) edit_slot = -1;
  else if (vk == VK_BACK && len) edit_buf[len - 1] = 0;
  return true;
}

// --ui-audit: slot 0 being named with the keyboard on screen (on: true), or not any more.
void dl_saves_osk_sample(bool on) {
  osk = on;
  edit_slot = on ? 0 : -1;
  osk_x = 3;
  osk_y = 1;
  osk_page = dl_cfg.ui_lang == 0 ? 4 : 0; // in Japanese, the second page of kana
  lstrcpyW(edit_buf, L"Abc");
}

// A on the keyboard on screen: the key in hand.
static void osk_press(void) {
  if (osk_y < 3) { dl_saves_char(osk_char(osk_page, osk_x, osk_y)); return; }
  switch (osk_x / 2) {
    case 0: osk_page = (osk_page + 1) % OSK_PAGES; break;
    case 1: osk_caps = !osk_caps; break;
    case 2: dl_saves_char(L' '); break;
    case 3: {
      int len = lstrlenW(edit_buf);
      if (len) edit_buf[len - 1] = 0;
      break;
    }
    default: osk = false; dl_saves_key(VK_RETURN); break; // OK
  }
}

void dl_saves_char(wchar_t c) {
  int len = lstrlenW(edit_buf);
  if (edit_slot < 0 || c < 32 || c == 127 || len >= NAME_MAX) return;
  edit_buf[len] = c;
  edit_buf[len + 1] = 0;
  dl_dirty = true;
}

const DlList dl_saves_list = {saves_open, saves_count, saves_row, saves_help, saves_enter, saves_erase, list_moved, NULL, saves_preview};

// ---- backups: the versions of the saves before they were written over, in saves\backup\
// ("<file>.1" the newest to "<file>.3", and "<file>.path" where it goes back). A new copy only when
// the newest is BK_GAP seconds old: a burst of writings keeps the version from before it.
#define BK_KEEP 3
#define BK_GAP (10 * 60)

static void bk_dir(char *out, bool make) {
  char saves[MAX_PATH];
  folder(saves, "saves", make);
  snprintf(out, MAX_PATH, "%s" DL_SEP "backup", saves);
  if (make) CreateDirectoryA(out, NULL);
}

static const char *leaf(const char *p) {
  const char *s = strrchr(p, DL_SEPC);
  return s ? s + 1 : p;
}

static void bk_path(char *out, const char *name, int k) {
  char d[MAX_PATH];
  bk_dir(d, false);
  if (k) snprintf(out, MAX_PATH, "%s" DL_SEP "%s.%d", d, name, k);
  else snprintf(out, MAX_PATH, "%s" DL_SEP "%s.path", d, name);
}

// A whole file in memory (free it), or NULL.
static char *read_all(const char *path, size_t *n) {
  FILE *f = fopen(path, "rb");
  if (!f) return NULL;
  fseek(f, 0, SEEK_END);
  long len = ftell(f);
  fseek(f, 0, SEEK_SET);
  char *buf = len >= 0 ? malloc((size_t)len + 1) : NULL;
  if (buf && fread(buf, 1, (size_t)len, f) != (size_t)len) { free(buf); buf = NULL; }
  fclose(f);
  if (buf) { buf[len] = 0; *n = (size_t)len; }
  return buf;
}

static bool write_all(const char *path, const char *data, size_t n) {
  FILE *f = fopen(path, "wb");
  if (!f) return false;
  bool ok = fwrite(data, 1, n, f) == n;
  return fclose(f) == 0 && ok;
}

// Seconds since the file was last written, or -1 when it is not there.
static long long age_of(const char *path) {
  WIN32_FILE_ATTRIBUTE_DATA fa;
  if (!GetFileAttributesExA(path, GetFileExInfoStandard, &fa)) return -1;
  unsigned long long t = (unsigned long long)fa.ftLastWriteTime.dwHighDateTime << 32 | fa.ftLastWriteTime.dwLowDateTime;
  return (long long)time(NULL) - (long long)(t / 10000000ull - 11644473600ull);
}

// The name of a file's copies: its own, and a fingerprint of its whole path (two saves of the same
// name in two folders, a language's each, are kept apart).
static void bk_name(const char *path, char *out) {
  unsigned h = 2166136261u;
  for (const char *c = path; *c; c++) h = (h ^ (unsigned char)*c) * 16777619u;
  snprintf(out, MAX_PATH, "%s.%08x", leaf(path), h);
}

static void backup_make(const char *path, bool force) {
  if (!file_exists(path)) return;
  char name[MAX_PATH];
  bk_name(path, name);
  char newest[MAX_PATH], a[MAX_PATH], b[MAX_PATH], d[MAX_PATH];
  bk_path(newest, name, 1);
  long long age = age_of(newest);
  if (!force && age >= 0 && age < BK_GAP) return;
  size_t n;
  char *data = read_all(path, &n);
  if (!data) return;
  bk_dir(d, true);
  bk_path(a, name, BK_KEEP);
  DeleteFileA(a);
  for (int k = BK_KEEP - 1; k >= 1; k--) {
    bk_path(a, name, k);
    bk_path(b, name, k + 1);
    rename(a, b);
  }
  write_all(newest, data, n);
  free(data);
  bk_path(a, name, 0);
  write_all(a, path, strlen(path));
}

void dl_backup(const char *path) { backup_make(path, false); }

#define BK_MAX 96
typedef struct { char name[MAX_PATH], where[MAX_PATH]; int k, state; FILETIME t; } Backup; // state: the save state n it is (-1 the session), or -2 a file of the game
static Backup bk[BK_MAX];
// A second press confirms (Enter puts back, Delete erases) when no other key came between: the menus
// call moved() before every key but Delete, so Enter, Enter counts one moved() between, a click none.
static int bk_n, bk_confirm = -1, bk_erase = -1, bk_keys;

// Which save state of DarkLauncher a path is: 0-9, DL_SLOTS + k, -1 the session; -2 none (a game's file).
static int state_of(const char *where) {
  for (int n = -1; n < states_count(); n++) {
    char p[MAX_PATH];
    dl_slot_path(p, n, "");
    if (!strcmp(p, where)) return n;
  }
  return -2;
}

static void bk_open(void) {
  char d[MAX_PATH], pattern[MAX_PATH];
  bk_dir(d, false);
  snprintf(pattern, sizeof pattern, "%s" DL_SEP "*.path", d);
  bk_n = 0;
  bk_confirm = bk_erase = -1;
  WIN32_FIND_DATAA fd;
  HANDLE h = FindFirstFileA(pattern, &fd);
  if (h == INVALID_HANDLE_VALUE) return;
  do {
    char name[MAX_PATH], where[MAX_PATH] = "", p[MAX_PATH];
    snprintf(name, sizeof name, "%s", fd.cFileName);
    char *dot = strrchr(name, '.');
    if (dot) *dot = 0;
    bk_path(p, name, 0);
    size_t n;
    char *w = read_all(p, &n);
    if (!w) continue;
    snprintf(where, sizeof where, "%s", w);
    free(w);
    for (int k = 1; k <= BK_KEEP && bk_n < BK_MAX; k++) {
      WIN32_FILE_ATTRIBUTE_DATA fa;
      bk_path(p, name, k);
      if (!GetFileAttributesExA(p, GetFileExInfoStandard, &fa)) continue;
      Backup *b = &bk[bk_n++];
      snprintf(b->name, sizeof b->name, "%s", name);
      snprintf(b->where, sizeof b->where, "%s", where);
      b->k = k;
      b->t = fa.ftLastWriteTime;
      b->state = state_of(where);
    }
  } while (FindNextFileA(h, &fd));
  FindClose(h);
  for (int i = 1; i < bk_n; i++) // the newest first
    for (int j = i; j > 0 && CompareFileTime(&bk[j].t, &bk[j - 1].t) > 0; j--) {
      Backup t = bk[j];
      bk[j] = bk[j - 1];
      bk[j - 1] = t;
    }
}

static int bk_count(void) { return bk_n; }

static void bk_row(int k, wchar_t *label, wchar_t *value) {
  const Backup *b = &bk[k];
  if (b->state == -1) lstrcpynW(label, TX(T_BK_SESSION), 64);
  else if (b->state >= 0 && b->state < DL_SLOTS && sv_name[b->state][0]) _snwprintf(label, 63, L"%d. %ls", b->state, sv_name[b->state]);
  else if (b->state >= 0 && b->state < DL_SLOTS) wsprintfW(label, TX(T_SLOT_N), b->state);
  else if (b->state >= DL_SLOTS && dl_game->auto_labels) lstrcpynW(label, dl_tr(dl_game->auto_labels[b->state - DL_SLOTS]), 64);
  else MultiByteToWideChar(CP_ACP, 0, leaf(b->where), -1, label, 64);
  label[63] = 0;
  row_date(&b->t, value, false);
}

static const wchar_t *bk_help(int k) {
  static wchar_t buf[600];
  if (bk_confirm == k && !bk_keys) return TX(T_BKH_CONFIRM);
  if (bk_erase == k && !bk_keys) return TX(T_SVH_CONFIRM);
  _snwprintf(buf, 599, L"%ls%ls", TX(T_BKH), bk[k].state == -2 ? TX(T_BKH_GAME) : L"");
  buf[599] = 0;
  return buf;
}

// Enter twice: the version goes back where it came from, the one there now kept in turn; a file of
// the game's own is read at start, so the program starts again.
static void bk_enter(int k) {
  if (bk_confirm != k || bk_keys > 1) {
    bk_confirm = k;
    bk_erase = -1;
    bk_keys = 0;
    return;
  }
  bk_confirm = -1;
  Backup b = bk[k];
  char p[MAX_PATH];
  size_t n;
  bk_path(p, b.name, b.k);
  char *data = read_all(p, &n);
  if (!data) return;
  backup_make(b.where, true);
  make_parent(b.where);
  bool ok = write_all(b.where, data, n);
  free(data);
  if (ok && b.state >= 0) { // a save state: its picture was of the version written over
    char pic[MAX_PATH];
    dl_slot_path(pic, b.state, ".png");
    DeleteFileA(pic);
  }
  if (ok && b.state == -2) { dl_relaunch(); return; }
  if (ok && b.state == -1 && dl_started) { dl_session_resume(); return; } // in play, the session would be written over at the close: taken up now
  if (ok) dl_note(TX(T_BK_RESTORED));
  bk_open();
  saves_open();
}

static void bk_erase_row(int k) {
  if (bk_erase != k || bk_keys > 0) {
    bk_erase = k;
    bk_confirm = -1;
    bk_keys = 0;
    return;
  }
  bk_erase = -1;
  char p[MAX_PATH];
  bk_path(p, bk[k].name, bk[k].k);
  DeleteFileA(p);
  bk_open();
}

static void bk_moved(void) { bk_keys++; }

const DlList dl_backups_list = {bk_open, bk_count, bk_row, bk_help, bk_enter, bk_erase_row, bk_moved, dl_texts[T_BK_NONE], NULL};

// screenshots: the pictures of the captures folder, newest first
#define SHOT_MAX 1000
typedef struct { char name[128]; FILETIME t; } Shot;
static Shot *shots;
static int shots_n;
static char shots_dir[MAX_PATH];

static int shot_newer(const void *a, const void *b) { return -CompareFileTime(&((const Shot *)a)->t, &((const Shot *)b)->t); }

static void shots_open(void) {
  pic_forget();
  confirm = -1;
  shots_n = 0;
  if (!shots) shots = malloc(sizeof *shots * SHOT_MAX);
  folder(shots_dir, "captures", false);
  if (!shots) return;
  char pat[MAX_PATH];
  snprintf(pat, sizeof pat, "%s" DL_SEP "*.png", shots_dir);
  WIN32_FIND_DATAA fd;
  HANDLE h = FindFirstFileA(pat, &fd);
  if (h == INVALID_HANDLE_VALUE) return;
  do {
    if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || strlen(fd.cFileName) >= sizeof shots->name) continue;
    snprintf(shots[shots_n].name, sizeof shots->name, "%s", fd.cFileName);
    shots[shots_n].t = fd.ftLastWriteTime;
    shots_n++;
  } while (shots_n < SHOT_MAX && FindNextFileA(h, &fd));
  FindClose(h);
  qsort(shots, (size_t)shots_n, sizeof *shots, shot_newer);
}
static int shots_count(void) { return shots_n; }
static void shots_row(int k, wchar_t *label, wchar_t *value) {
  row_date(&shots[k].t, label, true);
  value[0] = 0;
}
static void shot_path(int k, char *out) { snprintf(out, MAX_PATH, "%s" DL_SEP "%s", shots_dir, shots[k].name); }
static const wchar_t *shots_help(int k) {
  static wchar_t buf[512];
  if (confirm == k) return TX(T_SHH_CONFIRM);
  buf[0] = 0;
  if (pic_for == k && pic_w > 0) _snwprintf(buf, 200, TX(T_SHH_SIZE), pic_w, pic_h);
  buf[200] = 0;
  lstrcatW(buf, TX(T_SHH_OPEN));
  return buf;
}
static bool shots_preview(void *dcv, int k, double x0, double y0, double x1, double y1) {
  char p[MAX_PATH];
  int w, h;
  shot_path(k, p);
  HBITMAP bmp = pic_of(p, k, &w, &h);
  if (bmp && w > 0 && h > 0) dl_draw_picture(dcv, bmp, w, h, x0, y0, x1, y1);
  return true;
}
static void shots_enter(int k) {
  char p[MAX_PATH];
  shot_path(k, p);
  ShellExecuteA(dl_hwnd, "open", p, NULL, NULL, SW_SHOWNORMAL);
}
// a screenshot goes to the Recycle Bin, in two presses
static void shots_erase(int k) {
  if (confirm != k) { confirm = k; return; }
  char p[MAX_PATH + 1];
  shot_path(k, p);
  p[strlen(p) + 1] = 0; // the list of files ends with two zeros
  SHFILEOPSTRUCTA op = {0};
  op.hwnd = dl_hwnd;
  op.wFunc = FO_DELETE;
  op.pFrom = p;
  op.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_SILENT | FOF_NOERRORUI;
  SHFileOperationA(&op);
  shots_open();
}

const DlList dl_shots_list = {shots_open, shots_count, shots_row, shots_help, shots_enter, shots_erase, list_moved, dl_texts[T_SHH_NONE], shots_preview};
